# Crow + RocksDB SQL-ish Engine

Lightweight, fast JSON-backed SQL-style query layer over RocksDB with:
- In-memory secondary indexing for equality/inequality
- SQL-style `SELECT`, `WHERE` (with `AND`/`OR`), `LIKE`, range, date comparisons
- `JOIN` (multiple, inner and left), `GROUP BY`, `ORDER BY`
- `INSERT`/`UPDATE`/`DELETE`/`BATCH` with atomic semantics
- Alias support and basic query planning
- Simple key generation with special `id` handling
- Designed to embed with business logic (e.g., V8) for low-latency on-machine querying

## Table of Contents

- [Server Endpoint](#server-endpoint)  
- [Key Concepts](#key-concepts)  
- [API Commands](#api-commands)  
  - [Insert](#insert)  
  - [Select SQL](#select-sql)  
  - [Update](#update)  
  - [Delete](#delete)  
  - [Batch](#batch)  
- [SQL Features](#sql-features)  
  - [WHERE with AND / OR](#where-with-and--or)  
  - [LIKE and Range](#like-and-range)  
  - [JOINs (inner / left -- multiple)](#joins-inner--left----multiple)  
  - [GROUP BY / ORDER BY](#group-by--order-by)  
  - [Alias Resolution](#alias-resolution)  
- [Key Generation Logic](#key-generation-logic)  
- [Example cURL Requests](#example-curl-requests)  
- [JavaScript Test Harness](#javascript-test-harness)  
- [Error Handling](#error-handling)  
- [Best Practices & Notes](#best-practices--notes)

---

## Server Endpoint

All requests are POSTed to:

```
/query
```

(If the server runs on `http://localhost:18080`, full URL is `http://localhost:18080/query`.)

Content-Type must be `application/json`.

---

## Key Concepts

### Data Storage
- Rows are stored as JSON objects in RocksDB column families (each table is a column family).
- Secondary in-memory index: `index[table][field][value] -> list of keys` accelerates `field = 'x'` and `field != 'x'` queries.

### Key Generation
- If the JSON has `"id": <number>`, that number (as string) is used as the primary key.
- Otherwise the entire JSON is canonicalized (concatenated) and hashed to produce a unique key.
  - Example: `{ "name":"Alice", "age":27 }` → hash of `name=Alice;age=27;` string.

---

## API Commands

### Insert

Insert a record into a table. Creates the column family if missing.

**Payload:**
```json
{
  "command": "insert",
  "table": "users",
  "data": {
    "id": 1,
    "name": "John",
    "age": 35,
    "city": "London",
    "signup": "2024-01-15"
  }
}
```

**Response Example:**
```json
{
  "status": "inserted",
  "key": "1"
}
```

### Select SQL

Run an SQL-style query. Supports `SELECT`, `FROM`, `WHERE`, `JOIN`, `GROUP BY`, `ORDER BY`.

**Payload:**
```json
{
  "command": "select_sql",
  "sql": "SELECT * FROM users WHERE age > '30' AND name LIKE 'J%';"
}
```

**Response:** Array of JSON objects matching the query.

### Update

Update rows matching a `WHERE` clause. Uses same condition syntax as `SELECT`.

**Payload:**
```json
{
  "command": "update",
  "table": "users",
  "where": "name='John'",
  "data": {
    "city": "Amsterdam"
  }
}
```

**Response Example:**
```json
{
  "status": "updated",
  "count": 1
}
```

### Delete

Delete rows matching a `WHERE` clause.

**Payload:**
```json
{
  "command": "delete",
  "table": "users",
  "where": "age<'30'"
}
```

**Response Example:**
```json
{
  "status": "deleted",
  "count": 2
}
```

### Batch

Atomic batch of mixed operations. If one write fails, the entire batch does not commit.

**Payload Example:**
```json
{
  "command": "batch",
  "commands": [
    { "command": "insert", "table": "users", "data": { "id": 6, "name": "Bob", "age": 33 } },
    { "command": "update", "table": "users", "where": "id='6'", "data": { "city": "Oslo" } }
  ]
}
```

**Response:**
```json
{
  "status": "batch ok"
}
```

---

## SQL Features

### WHERE with AND / OR

Supports boolean combinations. OR is top-level; AND groups are intersected.

```sql
SELECT * FROM users WHERE city='London' AND age > '25' OR city='Paris';
```

Meaning:
```
(city = 'London' AND age > 25) OR (city = 'Paris')
```

### LIKE and Range

```sql
SELECT * FROM users WHERE name LIKE 'J%';                -- prefix match
SELECT * FROM users WHERE age >= '30' AND age <= '40';    -- numeric range
SELECT * FROM users WHERE signup >= '2024-01-01' AND signup <= '2024-12-31';  -- date range (ISO format)
```

`LIKE` is case-insensitive prefix/suffix support depending on implementation; typically `'J%'` matches values starting with `J`.

### JOINs (inner / left -- multiple)

Supports multiple equi-joins with aliasing. Inner and left join syntax:

```sql
SELECT u.name, o.amount
FROM users u
JOIN orders o ON u.id = o.user_id
WHERE o.amount > '100';

SELECT u.name, o.amount
FROM users u
LEFT JOIN orders o ON u.id = o.user_id
WHERE u.city='London';
```

Multiple joins are allowed in sequence:
```sql
SELECT * FROM a
JOIN b ON a.id = b.a_id
JOIN c ON b.id = c.b_id
WHERE ...
```

### GROUP BY / ORDER BY

```sql
SELECT * FROM orders WHERE amount > '50' GROUP BY user_id ORDER BY amount DESC;
SELECT * FROM users WHERE age > '20' ORDER BY age ASC;
```

### Alias Resolution

You can alias tables and refer to their columns in filters:

```sql
SELECT * FROM users u JOIN orders o ON u.id = o.user_id WHERE o.date >= '2025-01-01';
```

The parser normalizes `u` and `o` to real table names internally.

---

## Key Generation Logic

The `generate_key` function:

```cpp
std::string generate_key(const crow::json::rvalue& data) {
    if (data.has("id") && data["id"].t() == crow::json::type::Number)
        return std::to_string(data["id"].i());
    // fallback: hash of serialized content
}
```

- Numeric `id` becomes the key: stable for updates.
- Otherwise, a hash of the full object is used (canonical serialization).

---

## Example cURL Requests

Insert:
```bash
curl -X POST http://localhost:18080/query   -H "Content-Type: application/json"   -d '{"command":"insert","table":"users","data":{"id":1,"name":"John","age":35,"city":"London","signup":"2024-01-15"}}'
```

Select with WHERE:
```bash
curl -X POST http://localhost:18080/query   -H "Content-Type: application/json"   -d '{"command":"select_sql","sql":"SELECT * FROM users WHERE age > \'30\' AND name LIKE \'J%\';"}'
```

Join:
```bash
curl -X POST http://localhost:18080/query   -H "Content-Type: application/json"   -d '{"command":"select_sql","sql":"SELECT * FROM users u JOIN orders o ON u.id = o.user_id WHERE o.amount > \'100\';"}'
```

Update:
```bash
curl -X POST http://localhost:18080/query   -H "Content-Type: application/json"   -d '{"command":"update","table":"users","where":"name=\'John\'","data":{"city":"Amsterdam"}}'
```

Delete:
```bash
curl -X POST http://localhost:18080/query   -H "Content-Type: application/json"   -d '{"command":"delete","table":"users","where":"age<\'30\'"}'
```

Batch:
```bash
curl -X POST http://localhost:18080/query   -H "Content-Type: application/json"   -d '{
    "command":"batch",
    "commands":[
      {"command":"insert","table":"users","data":{"id":10,"name":"Zoe","age":26}},
      {"command":"update","table":"users","where":"id=\'10\'","data":{"city":"Lisbon"}}
    ]
  }'
```

---

## JavaScript Test Harness

There is a standalone `test.html` (single page) that:
- Lets you run all the above operations
- Provides example scenarios (inserts, joins, range, like, group/order)
- Displays results in a dynamic table
- Has endpoint override input for cross-origin testing

Open it from the server origin to avoid CORS (e.g., `http://localhost:18080/test.html`).

---

## Error Handling

- **Invalid SQL / parse errors**: returns JSON with `{"error":"..."}`
- **Parenthesis errors**: be sure your input has balanced parentheses (quotes are ignored by the robust checker).
- **Type issues**: code uses safe JSON extraction helpers to avoid `.s()` on non-string types.
- **Batch atomicity**: if any write in a batch fails, the entire batch is rejected (error will be returned).

---

## Best Practices & Notes

- **Use numeric `id` when possible** for stable keys and easier updates. You can extend to string IDs if desired.  
- **Date fields** should be ISO-formatted (`YYYY-MM-DD`) so lexicographical comparisons work for ranges.  
- **Indexes** only accelerate equality/inequality; `LIKE` and ranges currently scan. You can extend indexing structures for prefix/range if needed.  
- **Joins** currently support inner and left equi-joins. For outer semantics ensure your executor interprets `JoinType`.  
- **Alias usage**: Always qualify ambiguous columns in multi-table queries (`u.id` vs `o.user_id`) to avoid confusion.  
- **Concurrency**: Inserts are fast; updates/deletes acquire a lock to synchronize index maintenance.  
- **Extensibility**: Business logic (e.g., via embedded V8) can issue these JSON/SQL commands directly to this API for low-latency decisions.

