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

## 📄 License

MIT
