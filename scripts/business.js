 
// scripts/business.js
// Business Logic for Quarksql Accounting (Projects + Chart of Accounts + Journals + Reports)

var sanitize = (typeof sanitize !== 'undefined') ? sanitize : require('sanitize');
var auth = (typeof auth !== 'undefined') ? auth : require('auth');

var api = api || {};

// ---- Helpers ----
function requireUser(token){
  sanitize.requireAuth(token);
  var payloadJson = auth.verify(token); // JSON string
  var claims = JSON.parse(payloadJson);
  return claims.sub;
}

function uuid(){ return auth.randomId(12) + '-' + auth.randomId(12); }

var DEFAULT_ACCOUNTS = [
  // Assets
  { code:'1000', name:'Cash', type:'Asset' },
  { code:'1100', name:'Bank', type:'Asset' },
  { code:'1200', name:'Accounts Receivable', type:'Asset' },
  { code:'1300', name:'Inventory', type:'Asset' },
  // Liabilities
  { code:'2000', name:'Accounts Payable', type:'Liability' },
  { code:'2100', name:'Taxes Payable', type:'Liability' },
  // Equity
  { code:'3000', name:'Owner Equity', type:'Equity' },
  { code:'3100', name:'Retained Earnings', type:'Equity' },
  // Revenue
  { code:'4000', name:'Sales Revenue', type:'Revenue' },
  { code:'4100', name:'Service Revenue', type:'Revenue' },
  // Expenses
  { code:'5000', name:'Cost of Goods Sold', type:'Expense' },
  { code:'5100', name:'Rent', type:'Expense' },
  { code:'5200', name:'Utilities', type:'Expense' },
  { code:'5300', name:'Salaries', type:'Expense' },
  { code:'5400', name:'Marketing', type:'Expense' },
  { code:'5500', name:'Shopping', type:'Expense' }
];

function ensureAccountsForProject(project_id){
  // If no accounts for project, seed from 'global' or DEFAULT
  var rows = db.query("SELECT * FROM accounts WHERE project_id = '" + project_id + "';");
  if (rows && rows.length) return;
  // Try global
  var globalRows = db.query("SELECT * FROM accounts WHERE project_id = 'global';");
  var seed = (globalRows && globalRows.length) ? globalRows : DEFAULT_ACCOUNTS;
  seed.forEach(function(acc) {
    var row = { id: uuid(), project_id, code: acc.code, name: acc.name, type: acc.type, is_active: true };
    db.execute("INSERT INTO accounts VALUES " + JSON.stringify(row) + ";");
  });
}

function assertAccountExists(project_id, account_code){
  var rr = db.query("SELECT * FROM accounts WHERE project_id = '" + project_id + "' AND code = '" + account_code + "';");
  if (!rr || !rr.length) throw new Error("Account " + account_code + " not found for project " + project_id);
}

function toIsoDate(d){ return new Date(d).toISOString().slice(0,10); }

// ---- Auth ----

api.signup = {
  params: ['username','password'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    var res = auth.signup(p.username, p.password);
    return res;
  }
};

api.login = {
  params: ['username','password'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    var token = auth.login(p.username, p.password);
    return { token: token };
  }
};

api.verify = {
  params: ['token'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    var payloadJson = auth.verify(p.token);
    return { data: JSON.parse(payloadJson) };
  }
};

// ---- Projects ----

api.createProject = {
  params: ['token','name'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    var user = requireUser(p.token);
    var id = uuid();
    var row = { id: id, name: sanitize.nonEmptyString(p.name,'name'), created_by: user, created_at: new Date().toISOString() };
    db.execute("INSERT INTO projects VALUES " + JSON.stringify(row) + ";");
    ensureAccountsForProject(id);
    return { project: row };
  }
};

api.listProjects = {
  params: ['token'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    var rows = db.query("SELECT * FROM projects;");
    return { projects: rows || [] };
  }
};

// ---- Accounts ----

api.seedAccountsGlobal = {
  params: ['token'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    // Seed a global template once
    var existing = db.query("SELECT * FROM accounts WHERE project_id = 'global';");
    if (existing && existing.length) return { seeded: 0, note: 'Already present' };
    DEFAULT_ACCOUNTS.forEach(function(acc) {
      var row = { id: uuid(), project_id: 'global', code: acc.code, name: acc.name, type: acc.type, is_active: true };
      db.execute("INSERT INTO accounts VALUES " + JSON.stringify(row) + ";");
    });
    return { seeded: DEFAULT_ACCOUNTS.length };
  }
};

api.listAccounts = {
  params: ['token','project_id'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    var rows = db.query("SELECT * FROM accounts WHERE project_id = '" + p.project_id + "' ORDER BY code ASC;");
    if (!rows || !rows.length) {
      // First access: ensure defaults exist for this project, then re-query
      ensureAccountsForProject(p.project_id);
      rows = db.query("SELECT * FROM accounts WHERE project_id = '" + p.project_id + "' ORDER BY code ASC;");
    }
    return { accounts: rows || [] };
  }
};

api.addAccount = {
  params: ['token','project_id','code','name','type'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    var row = { id: uuid(), project_id: p.project_id, code: p.code.trim(), name: p.name.trim(), type: p.type, is_active: true };
    db.execute("INSERT INTO accounts VALUES " + JSON.stringify(row) + ";");
    return { account: row };
  }
};

// ---- Journals ----

api.postJournal = {
  params: ['token','project_id','date','memo','lines'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    var user = requireUser(p.token);
    var project_id = p.project_id;
    ensureAccountsForProject(project_id);
    sanitize.isoDate(p.date);
    sanitize.ensureBalancedLines(p.lines);
    // Ensure accounts exist
    p.lines.forEach(function(l){ assertAccountExists(project_id, l.account_code); });
    var entry_id = uuid();
    var entry = {
      id: entry_id,
      project_id,
      date: toIsoDate(p.date),
      memo: (p.memo||'').toString(),
      created_by: user,
      created_at: new Date().toISOString()
    };
    // Persist header so reports (which JOIN on entries) can see it
    db.execute("INSERT INTO journal_entries VALUES " + JSON.stringify(entry) + ";");
    // Lines
    var line_no = 1;
    p.lines.forEach(function(l) {
      var line = {
        id: entry_id + '#' + line_no++,
        entry_id: entry_id,
        project_id,
        line_no: line_no-1,
        account_code: l.account_code,
        debit: +(l.debit||0),
        credit: +(l.credit||0)
      };
      db.execute("INSERT INTO journal_lines VALUES " + JSON.stringify(line) + ";");
    });
    return { entry: entry };
  }
};

api.listJournals = {
  params: ['token','project_id','skip','limit'],
  handler: function(p){
    sanitize.checkParams(p, ['token','project_id']);
    requireUser(p.token);
    var skip = p.skip ? +p.skip : 0;
    var limit = p.limit ? +p.limit : 100;
    var rows = db.query("SELECT * FROM journal_entries WHERE project_id = '" + p.project_id + "' ORDER BY date DESC SKIP " + skip + " LIMIT " + limit + ";");
    return { entries: rows || [] };
  }
};

api.getJournal = {
  params: ['token','entry_id'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    var header = db.query("SELECT * FROM journal_entries WHERE id = '" + p.entry_id + "';");
    var lines = db.query("SELECT * FROM journal_lines WHERE entry_id = '" + p.entry_id + "' ORDER BY line_no ASC;");
    return { entry: header && header[0], lines: lines || [] };
  }
};

// ---- Reports ----

// trialBalanceQuery removed; aggregation handled in JS


api.trialBalance = {
  params: ['token','project_id'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    var sql = "SELECT account_code, SUM(debit) AS debit, SUM(credit) AS credit " +
              "FROM journal_lines WHERE project_id = '" + p.project_id + "' " +
              "GROUP BY account_code ORDER BY account_code ASC;";
    var rows = db.query(sql) || [];
    var names = db.query("SELECT code,name,type FROM accounts WHERE project_id = '" + p.project_id + "';") || [];
    var nameByCode = {}; names.forEach(function(a){ nameByCode[a.code] = {name:a.name, type:a.type}; });
    rows.forEach(function(r){
      var info = nameByCode[r.account_code] || {name:'(Unknown)', type:'Unknown'};
      r.name = info.name; r.type = info.type;
      r.balance = +(+(r.debit||0) - +(r.credit||0)).toFixed(2);
    });
    return { rows: rows };
  }
};

api.profitAndLoss = {
  params: ['token','project_id','from','to'],
  handler: function(p){
    sanitize.checkParams(p, ['token','project_id']);
    requireUser(p.token);
    var from = p.from ? " AND e.date >= '" + sanitize.isoDate(p.from,'from') + "'" : "";
    var to   = p.to   ? " AND e.date <= '" + sanitize.isoDate(p.to,'to') + "'"   : "";
    var sql = "SELECT l.account_code, SUM(l.debit) AS debit, SUM(l.credit) AS credit " +
              "FROM journal_lines l JOIN journal_entries e ON l.entry_id = e.id " +
              "WHERE l.project_id = '" + p.project_id + "'" + from + to + " " +
              "GROUP BY l.account_code;";
    var rows = db.query(sql) || [];
    var names = db.query("SELECT code,name,type FROM accounts WHERE project_id = '" + p.project_id + "';") || [];
    var nameByCode = {}; names.forEach(function(a){ nameByCode[a.code] = {name:a.name, type:a.type}; });
    var revenue = 0, expense = 0;
    rows.forEach(function(r){
      var t = (nameByCode[r.account_code]||{}).type || 'Unknown';
      var bal = +(+(r.debit||0) - +(r.credit||0)).toFixed(2);
      if (t === 'Revenue') revenue += -bal;
      if (t === 'Expense') expense += bal;
    });
    var net = +(revenue - expense).toFixed(2);
    return { revenue: +revenue.toFixed(2), expense: +expense.toFixed(2), net_income: net };
  }
};

// ---- Overall (across projects) ----

api.overallTrialBalance = {
  params: ['token'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    // Do aggregation in JS to avoid SQL engine's current single-column GROUP BY limitation
    var rows = db.query("SELECT * FROM journal_lines;") || [];
    var acc = {};
    for (var i=0;i<rows.length;i++){
      var r = rows[i];
      var key = r.project_id + '|' + r.account_code;
      if (!acc[key]) acc[key] = { project_id: r.project_id, account_code: r.account_code, debit: 0, credit: 0 };
      acc[key].debit  += +(r.debit || 0);
      acc[key].credit += +(r.credit|| 0);
    }
    var out = Object.keys(acc).sort().map(function(k){ return acc[k]; });
    return { rows: out };
  }
};

api.overallProfitAndLoss = {
  params: ['token','from','to'],
  handler: function(p){
    sanitize.checkParams(p, ['token']);
    requireUser(p.token);
    var from = p.from ? " AND e.date >= '" + sanitize.isoDate(p.from,'from') + "'" : "";
    var to   = p.to   ? " AND e.date <= '" + sanitize.isoDate(p.to,'to') + "'"   : "";
    var sql = "SELECT l.project_id, l.account_code, SUM(l.debit) AS debit, SUM(l.credit) AS credit " +
              "FROM journal_lines l JOIN journal_entries e ON l.entry_id = e.id WHERE 1=1 " + from + to +
              " GROUP BY l.project_id, l.account_code;";
    var rows = db.query(sql) || [];
    return { rows: rows };
  }
};

// ---- Utility ----
api.list = {
  params: [],
  handler: function(){ return Object.keys(api).map(function(fn){ return {name:fn, params:api[fn].params}; }); }
};


// ---- Project-specific reports & drill-down ----

// Balance Sheet per project (optionally as of date)
api.balanceSheet = {
  params: ['token','project_id','as_of'],
  handler: function(p){
    sanitize.checkParams(p, ['token','project_id']);
    requireUser(p.token);
    var asOf = p.as_of ? " AND e.date <= '" + sanitize.isoDate(p.as_of,'as_of') + "'" : "";
    var sql = "SELECT l.account_code, SUM(l.debit) AS debit, SUM(l.credit) AS credit " +
              "FROM journal_lines l JOIN journal_entries e ON l.entry_id = e.id " +
              "WHERE l.project_id = '" + p.project_id + "'" + asOf + " GROUP BY l.account_code;";
    var rows = db.query(sql) || [];
    var names = db.query("SELECT code,name,type FROM accounts WHERE project_id = '" + p.project_id + "';") || [];
    var nameByCode = {}; names.forEach(function(a){ nameByCode[a.code] = {name:a.name, type:a.type}; });
    var assets=[], liabilities=[], equity=[];
    var earnings = 0.0;
	rows.forEach(function(r){
      var info = nameByCode[r.account_code] || {name:r.account_code, type:'Unknown'};
      var bal = +(+(r.debit||0) - +(r.credit||0)).toFixed(2);
      var row = { code:r.account_code, name:info.name, balance: bal };
      if (info.type==='Asset') assets.push(row);
      else if (info.type==='Liability') liabilities.push(row);
      else if (info.type==='Equity') equity.push(row);
      else if (info.type==='Revenue') earnings+=bal;
	  else if (info.type==='Expense') earnings+=bal;
    });
    
    if(earnings != 0.0){
      var eRow = { code:'0000', name:'Earnings before closing', balance: earnings };
      equity.push(eRow);
	}
    
    function total(arr){ var s=0; for (var i=0;i<arr.length;i++){ s += +(+arr[i].balance||0); } return +s.toFixed(2); }
    return { assets: assets, liabilities: liabilities, equity: equity, totals: { assets: total(assets), liabilities: total(liabilities), equity: total(equity) } };
  }
};

// Profit & Loss per project (optionally between dates)
api.profitAndLoss = {
  params: ['token','project_id','from','to'],
  handler: function(p){
    sanitize.checkParams(p, ['token','project_id']);
    requireUser(p.token);
    // Always use LEFT JOIN; apply date filters on e.date when provided
    var from = p.from ? " AND e.date >= '" + sanitize.isoDate(p.from,'from') + "'" : "";
    var to   = p.to   ? " AND e.date <= '" + sanitize.isoDate(p.to,'to') + "'"   : "";
    var sql = "SELECT l.account_code, SUM(l.debit) AS debit, SUM(l.credit) AS credit " +
              "FROM journal_lines l LEFT JOIN journal_entries e ON l.entry_id = e.id " +
              "WHERE l.project_id = '" + p.project_id + "'" + from + to + " GROUP BY l.account_code;";
    var rows = db.query(sql) || [];
    var names = db.query("SELECT code,type FROM accounts WHERE project_id = '" + p.project_id + "';") || [];
    var typeByCode = {}; names.forEach(function(a){ typeByCode[a.code] = a.type; });
    var revenue = 0, expense = 0;
    rows.forEach(function(r){
      var t = typeByCode[r.account_code] || 'Unknown';
      var debit = +(r.debit||0), credit = +(r.credit||0);
      if (t === 'Revenue') revenue += +(credit - debit);
      if (t === 'Expense') expense += +(debit - credit);
    });
    revenue = +(+revenue).toFixed(2);
    expense = +(+expense).toFixed(2);
    var net = +(revenue - expense).toFixed(2);
    return { revenue: revenue, expense: expense, net_income: net };
  }
};

// Account ledger drill-down
api.accountLedger = {
  params: ['token','project_id','account_code','from','to'],
  handler: function(p){
    sanitize.checkParams(p, ['token','project_id','account_code']);
    requireUser(p.token);
    // Start with lines only to ensure we include legacy data without headers
    var baseSql = "SELECT entry_id, line_no, debit, credit FROM journal_lines WHERE project_id = '" + p.project_id + "' AND account_code = '" + p.account_code + "' ORDER BY entry_id ASC, line_no ASC;";
    var lines = db.query(baseSql) || [];
    // Build a cache of headers by entry_id (if present)
    var headerById = {};
    function getHeader(id){
      if (headerById[id] !== undefined) return headerById[id];
      var h = db.query("SELECT id, date, memo FROM journal_entries WHERE id = '" + id + "';");
      headerById[id] = (h && h[0]) ? h[0] : null;
      return headerById[id];
    }
    var running = 0;
    var out = [];
    for (var i=0;i<lines.length;i++){
      var L = lines[i];
      var H = getHeader(L.entry_id);
      running += +((L.debit||0) - (L.credit||0));
      out.push({
        entry_id: L.entry_id,
        date: H && H.date ? H.date : '',
        memo: H && H.memo ? H.memo : '',
        line_no: L.line_no,
        debit: L.debit||0,
        credit: L.credit||0,
        balance: +running.toFixed(2)
      });
    }
    return { ledger: out };
  }
};


// ---- VoiceAccounting report endpoints (added, non-breaking) ----
api.voiceTrialBalance = {
  params: ['token','project_id','from','to'],
  handler: function(p){
    sanitize.checkParams(p, ['token','project_id']);
    requireUser(p.token);
    var from = p.from ? " AND e.date >= '" + sanitize.isoDate(p.from,'from') + "'" : "";
    var to   = p.to   ? " AND e.date <= '" + sanitize.isoDate(p.to,'to') + "'"   : "";
    var sql = "SELECT l.account_code, SUM(l.debit) AS debit, SUM(l.credit) AS credit " +
              "FROM journal_lines l LEFT JOIN journal_entries e ON l.entry_id = e.id " +
              "WHERE l.project_id = '" + p.project_id + "'" + from + to + " GROUP BY l.account_code ORDER BY l.account_code ASC;";
    var rows = db.query(sql) || [];
    var names = db.query("SELECT code,name,type FROM accounts WHERE project_id = '" + p.project_id + "';") || [];
    var nameByCode = {}; names.forEach(function(a){ nameByCode[a.code] = {name:a.name, type:a.type}; });
    rows.forEach(function(r){
      var info = nameByCode[r.account_code] || {name:'(Unknown)', type:'Unknown'};
      r.name = info.name; r.type = info.type;
      r.balance = +(+(r.debit||0) - +(r.credit||0)).toFixed(2);
    });
    return { rows: rows };
  }
};

api.voiceProfitAndLoss = {
  params: ['token','project_id','from','to'],
  handler: function(p){
    sanitize.checkParams(p, ['token','project_id']);
    requireUser(p.token);
    var from = p.from ? " AND e.date >= '" + sanitize.isoDate(p.from,'from') + "'" : "";
    var to   = p.to   ? " AND e.date <= '" + sanitize.isoDate(p.to,'to') + "'"   : "";
    var sql = "SELECT l.account_code, SUM(l.debit) AS debit, SUM(l.credit) AS credit " +
              "FROM journal_lines l LEFT JOIN journal_entries e ON l.entry_id = e.id " +
              "WHERE l.project_id = '" + p.project_id + "'" + from + to + " GROUP BY l.account_code;";
    var rows = db.query(sql) || [];
    var names = db.query("SELECT code,type FROM accounts WHERE project_id = '" + p.project_id + "';") || [];
    var typeByCode = {}; names.forEach(function(a){ typeByCode[a.code] = a.type; });
    var revenue = 0, expense = 0;
    rows.forEach(function(r){
      var t = typeByCode[r.account_code] || 'Unknown';
      var debit = +(r.debit||0), credit = +(r.credit||0);
      if (t === 'Revenue') revenue += +(credit - debit);
      if (t === 'Expense') expense += +(debit - credit);
    });
    revenue = +(+revenue).toFixed(2);
    expense = +(+expense).toFixed(2);
    var net = +(revenue - expense).toFixed(2);
    return { revenue: revenue, expense: expense, net_income: net };
  }
};

api.voiceBalanceSheet = {
  params: ['token','project_id','as_of'],
  handler: function(p){
    sanitize.checkParams(p, ['token','project_id']);
    requireUser(p.token);
    var asOf = p.as_of ? " AND e.date <= '" + sanitize.isoDate(p.as_of,'as_of') + "'" : "";
    var sql = "SELECT l.account_code, SUM(l.debit) AS debit, SUM(l.credit) AS credit " +
              "FROM journal_lines l LEFT JOIN journal_entries e ON l.entry_id = e.id " +
              "WHERE l.project_id = '" + p.project_id + "'" + asOf + " GROUP BY l.account_code;";
    var rows = db.query(sql) || [];
    var names = db.query("SELECT code,name,type FROM accounts WHERE project_id = '" + p.project_id + "';") || [];
    var nameByCode = {}; names.forEach(function(a){ nameByCode[a.code] = {name:a.name, type:a.type}; });
    var assets=[], liabilities=[], equity=[];
    var earnings = 0.0;
    rows.forEach(function(r){
      var info = nameByCode[r.account_code] || {name:r.account_code, type:'Unknown'};
      var bal = +(+(r.debit||0) - +(r.credit||0)).toFixed(2);
      var row = { code:r.account_code, name:info.name, balance: bal };
      if (info.type==='Asset') assets.push(row);
      else if (info.type==='Liability') liabilities.push(row);
      else if (info.type==='Equity') equity.push(row);
      else if (info.type==='Revenue') earnings+=bal;
      else if (info.type==='Expense') earnings+=bal;
    });
    if(earnings != 0.0){ equity.push({ code:'0000', name:'Earnings before closing', balance: earnings }); }
    function total(arr){ var s=0; for (var i=0;i<arr.length;i++){ s += +(+arr[i].balance||0); } return +s.toFixed(2); }
    return { assets: assets, liabilities: liabilities, equity: equity, totals: { assets: total(assets), liabilities: total(liabilities), equity: total(equity) } };
  }
};

api.voiceAccountLedger = {
  params: ['token','project_id','account_code','from','to'],
  handler: function(p){
    sanitize.checkParams(p, ['token','project_id','account_code']);
    requireUser(p.token);
    var from = p.from ? " AND e.date >= '" + sanitize.isoDate(p.from,'from') + "'" : "";
    var to   = p.to   ? " AND e.date <= '" + sanitize.isoDate(p.to,'to') + "'"   : "";
    var sql = "SELECT entry_id, line_no, debit, credit, date, memo " +
              "FROM journal_lines l LEFT JOIN journal_entries e ON l.entry_id = e.id " +
              "WHERE l.project_id = '" + p.project_id + "' AND l.account_code = '" + p.account_code + "'" + from + to +
              " ORDER BY e.date ASC, l.entry_id ASC, l.line_no ASC;";
              
              
    var rows = db.query(sql) || [];
    var running = 0; var out = [];
    for (var i=0;i<rows.length;i++){
      var L = rows[i];
      running += +((L.debit||0) - (L.credit||0));
      out.push({ entry_id: L.entry_id, date: L.date||'', memo: L.memo||'', line_no: L.line_no, debit: L.debit||0, credit: L.credit||0, balance: +running.toFixed(2) });
    }
    return { ledger: out };
  }
};
