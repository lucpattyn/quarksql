# Title: Accounting Console

Slug: acct

This app exposes basic accounting endpoints to list projects and accounts,
query journals, and add accounts. You can edit this file and save; the server
will detect changes and regenerate assets automatically.

```json requirements
{
  "name": "Accounting Console",
  "slug": "acct",
  "api": [
    { "name": "acct_projects_list", "params": ["token"], "sql": "SELECT * FROM projects ORDER BY created_at DESC;" },
    { "name": "acct_accounts_by_project", "params": ["token","project_id"], "sql": "SELECT * FROM accounts WHERE project_id='${project_id}' ORDER BY code;" },
    { "name": "acct_journals_by_project", "params": ["token","project_id","skip","limit"], "sql": "SELECT * FROM journal_entries WHERE project_id='${project_id}' ORDER BY date DESC SKIP ${skip} LIMIT ${limit};" },
    { "name": "acct_add_account", "params": ["token","project_id","code","name","type"], "exec": "INSERT INTO accounts VALUES {\"id\":\"${code}\",\"project_id\":\"${project_id}\",\"code\":\"${code}\",\"name\":\"${name}\",\"type\":\"${type}\",\"is_active\":true};" }
  ]
}
```

