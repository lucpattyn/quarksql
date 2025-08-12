## Requirements Watcher

Drop a markdown file (e.g., `agent.md`) into this directory to auto-generate:
- `scripts/<slug>.js` business logic module (extending global `api`)
- `public/<slug>/{index.html, app.js, style.css}` client UI

The server watches this folder at runtime. When a file is added or modified, it will:
- Parse the file for basic metadata (name, slug) and optional API specs
- Generate files accordingly
- Append `require('<slug>')` to `scripts/business.js` if missing
- Hot-load the new module into V8 so endpoints are available immediately

### Spec (simple)

- App name: first `# Heading` or a `Title: ...` line
- Slug: optional `Slug: my-app` line; otherwise auto-slugify the name
- API: optional fenced JSON block with `requirements` tag; if omitted, a default hello endpoint is created.

```json requirements
{
  "name": "Inventory Manager",
  "slug": "inventory",
  "api": [
    { "name": "inventory_list", "params": ["token"], "sql": "SELECT * FROM products ORDER BY name ASC;" },
    { "name": "inventory_add",  "params": ["token","id","name"], "exec": "INSERT INTO products VALUES {\"id\":\"${id}\",\"name\":\"${name}\"};" }
  ]
}
```

Notes:
- For `sql` (SELECT) endpoints, the generated handler uses `db.query(sql)`.
- For `exec` (INSERT/UPDATE/DELETE/BATCH) endpoints, the handler uses `db.execute(sql)` and returns `{ ok: true }`.
- `${param}` placeholders in `sql`/`exec` are replaced at runtime from the POST body.
- If your endpoint requires auth, include `"token"` in `params`; it will be validated by `sanitize.requireAuth`.

### Client UI

A minimal UI is generated to:
- Select an endpoint
- Provide JSON parameters (including optional `token`)
- Run the call and show raw JSON response

Open: `/public/<slug>/index.html`

