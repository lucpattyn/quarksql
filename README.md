# Quarksql

Quarksql is a **lightweight, embeddable SQL‑like engine for C++** running on top of **RocksDB**, with a **Crow** HTTP API server, **V8** JavaScript integration, **JWT authentication**, and **JSON-based schemas** supporting in‑memory secondary indexing.

## ✨ Features

- **SQL syntax**: SELECT (WHERE, LIKE, ranges, JOIN, GROUP BY, ORDER BY, COUNT, SKIP, LIMIT), INSERT, UPDATE, DELETE, BATCH.
- **RocksDB**: One column family per table for efficient isolation and scanning.
- **IndexManager**: Maintains in-memory multimap indices for fast equality lookups and joins.
- **Push-down pagination**: SKIP/LIMIT applied during RocksDB iteration.
- **V8 JS logic**: Hooks in `scripts/business.js`, `auth.js`, `sanitize.js`.
- **Crow HTTP + JWT**: REST API plus interactive web UI from `public/index.html`.

## 📦 Structure

```
/src        → C++ source
/include    → C++ headers
/public     → index.html console
/scripts    → business/auth/sanitize JS
schemas.json
```

After building, copy `schemas.json`, `scripts/`, and `public/` into `build/`.

## ⚙️ Install & Build (Ubuntu 22.04)

```bash
sudo apt update
sudo apt install -y build-essential cmake librocksdb-dev libv8-dev libv8-headers libssl-dev
git clone https://github.com/lucpattyn/quarksql.git
cd quarksql
mkdir build && cd build
cmake .. -DCMAKE_CXX_STANDARD=17
cmake --build . -- -j$(nproc)
```

Then copy into `build/`:
- `schemas.json`
- `scripts/`
- `public/`

## 🚀 Run

```bash
./quarksql-server --dbpath ./data --port 18080 --jwt-secret mysecret
```

Visit `http://localhost:18080/` for the interactive console.

## 🌐 REST API

All endpoints are POST `/api/<function>` with JSON.

| Endpoint | Description |
|----------|-------------|
| `/api/login` | `{username,password}` → `{token}` |
| `/api/verify` | `{token}` → validity |
| `/api/query` | Run SELECT |
| `/api/execute` | Run INSERT/UPDATE/DELETE/BATCH |

## ✅ Supported SQL

### SELECT
```sql
SELECT * FROM users;
SELECT * FROM products WHERE price > '20';
SELECT * FROM products ORDER BY stock DESC SKIP 1 LIMIT 3;
SELECT COUNT(*) FROM orders;
SELECT user, COUNT(*) FROM orders GROUP BY user ORDER BY COUNT DESC;
SELECT orders.id, users.email FROM orders JOIN users ON orders.user = users.email;
```

### INSERT
```sql
INSERT INTO users VALUES {"email":"alice@example.com","password":"secret"};
```

### UPDATE
```sql
UPDATE users SET {"password":"newsecret"} WHERE email='alice@example.com';
```

### DELETE
```sql
DELETE FROM users WHERE email='alice@example.com';
DELETE FROM users KEYS ["alice@example.com"];
```

### BATCH
```sql
BATCH products {"p1":{"id":"p1","name":"Widget"},"p2":{"id":"p2","name":"Gadget"}};
```

## 🧪 Interactive Console

`public/index.html` provides ready-to-run query examples for SELECT, AGGREGATION, JOIN, and WRITE commands.

## Business Logic Layer Overview

# Quarksql — Business Logic–Driven SQL Engine

key differentiator: an embedded **V8 JavaScript engine** that runs your **business logic** directly inside the server process, giving you the flexibility of scripting with the speed of native C++ data access.

---

## ✨ Core Highlights

- **Embedded SQL engine** with:
  - `SELECT` (WHERE, LIKE, ranges, JOIN, GROUP BY, ORDER BY, COUNT, SKIP/LIMIT)
  - `INSERT`, `UPDATE`, `DELETE`, `BATCH`
- **Schema-driven** (JSON schemas define tables and indexed fields)
- **RocksDB column families** for table-level isolation
- **In-memory indices** for fast joins and equality queries
- **Push-down pagination** (skip/limit applied at scan)
- **Crow HTTP server** with JWT middleware
- **Business Logic Layer** in JavaScript via **V8**:
  - Write application rules in `scripts/business.js`
  - Call into C++ for database work
  - Keep auth, sanitization, and validation in `scripts/auth.js` and `scripts/sanitize.js`

---

## 🧠 Business Logic Layer (V8 + JavaScript)

The **business logic layer** is where you define your application’s **rules**, **permissions**, **transformations**, and **API surface**.  
It’s implemented in JavaScript, runs inside V8 embedded in the Quarksql process, and has **direct access to C++ bindings** for database and JWT operations.

### JS Modules

- **`business.js`** — Defines `api.*` methods that will be exposed to HTTP clients:
  - `api.login(username, password)` — Authenticates and issues JWT
  - `api.verify(token)` — Verifies JWT and returns claims
  - `api.query(sql)` — Executes SELECT queries
  - `api.execute(sql)` — Executes INSERT/UPDATE/DELETE/BATCH
- **`auth.js`** — Wraps JWT functions exported from C++
  - `CppSignJwt(claims_json)`
  - `CppVerifyJwt(token)`
- **`sanitize.js`** — Validates and cleans incoming parameters before execution

### How C++ Integrates with JS

1. **Startup**:
   - V8 Isolate & Context created in `main.cpp`
   - C++ functions bound into JS runtime:
     - `CppSignJwt`, `CppVerifyJwt`
     - `CppQuery`, `CppExecute`
2. **Scripts loaded**:
   - `auth.js`, `sanitize.js`, and `business.js` are loaded into the context
   - `globalThis.api` object is populated by `business.js`
3. **HTTP calls → JS**:
   - `/api/login` → calls `api.login(...)` in JS
   - `/api/verify` → calls `api.verify(token)`
   - `/api/query` → calls `api.query(sql)`
   - `/api/execute` → calls `api.execute(sql)`

### Example: `business.js` (simplified)

```js
const auth = require('auth');
const sanitize = require('sanitize');

globalThis.api = {};

api.login = (username, password) => {
  sanitize.credentials(username, password);
  const claims = { sub: username, iat: Date.now()/1000 };
  return { token: CppSignJwt(JSON.stringify(claims)) };
};

api.verify = (token) => {
  const payload = CppVerifyJwt(token);
  return { valid: true, data: JSON.parse(payload) };
};

api.query = (sql) => {
  sanitize.sql(sql);
  return JSON.parse(CppQuery(sql));
};

api.execute = (sql) => {
  sanitize.sql(sql);
  return JSON.parse(CppExecute(sql));
};

module.exports = api;
```

---

## 📜 Example HTTP Flow with Business Logic

1. **Login**:
   ```http
   POST /api/login
   Content-Type: application/json

   { "username": "alice", "password": "secret" }
   ```
   `api.login` runs in JS, calls `CppSignJwt` in C++ to produce a token.

2. **Query**:
   ```http
   POST /api/query
   Authorization: Bearer <jwt>
   Content-Type: application/json

   { "sql": "SELECT * FROM orders WHERE qty > '5';" }
   ```
   JS sanitizes the SQL, calls `CppQuery` (C++ parses → executes → returns JSON string), JS parses it and returns to HTTP.

---

## 📦 Build & Run

### Install Dependencies (Ubuntu 22.04)

```bash
sudo apt update
sudo apt install -y build-essential cmake librocksdb-dev                     libv8-dev libv8-headers libssl-dev                     nlohmann-json3-dev
```

### Build

```bash
git clone https://github.com/lucpattyn/quarksql.git
cd quarksql
mkdir build && cd build
cmake .. -DCMAKE_CXX_STANDARD=17
cmake --build . -- -j$(nproc)
```

### Prepare `build/` directory
Copy:
- `schemas.json`
- `public/` (contains `index.html`)
- `scripts/` (`business.js`, `auth.js`, `sanitize.js`)

### Run

```bash
./quarksql-server --dbpath ./data --port 18080 --jwt-secret mysecret
```

---

## 🌐 API Overview

| Endpoint       | Calls in JS       | Purpose                         |
|----------------|-------------------|---------------------------------|
| `/api/login`   | `api.login`       | Auth & token issuance           |
| `/api/verify`  | `api.verify`      | Token verification              |
| `/api/query`   | `api.query`       | Run SELECT queries              |
| `/api/execute` | `api.execute`     | Run INSERT/UPDATE/DELETE/BATCH  |

---

## 💡 Why Business Logic in JS?

- **Rapid iteration** — change application rules without recompiling C++
- **Separation of concerns** — database core in C++, API rules in JS
- **Sandboxed execution** — V8 isolates JS from direct system calls
- **Extensibility** — add new API endpoints by simply adding new `api.*` functions

---

## 📄 License

MIT

## 📄 License

MIT
