(function(){
  // Minimal install prompt handling
  var deferredPrompt = null;
  var installBtn = document.getElementById('installBtn');
  window.addEventListener('beforeinstallprompt', function(e){
    e.preventDefault();
    deferredPrompt = e;
    installBtn.hidden = false;
  });
  installBtn && installBtn.addEventListener('click', function(){
    if (!deferredPrompt) return;
    deferredPrompt.prompt();
    deferredPrompt = null;
    installBtn.hidden = true;
  });

  // Helpers
  function $(id){ return document.getElementById(id); }
  function setText(id, s){ var el=$(id); if(el) el.textContent = s || ''; }

  // Default date = today
  (function(){
    var d = new Date();
    var iso = new Date(d.getTime()-d.getTimezoneOffset()*60000).toISOString().slice(0,10);
    $('date').value = iso;
  

  function loadAccounts(projectId){
    if (!token || !projectId) return Promise.resolve();
    setText('parseStatus','Loading accounts...');
    return postJSON('/api/listAccounts', { token: token, project_id: projectId })
      .then(function(res){ indexAccounts(res.accounts||[]); setText('parseStatus','Accounts loaded: '+accounts.length); populateAccountDatalist(); })
      .catch(function(err){ setText('parseStatus','Accounts load failed: '+err.message); });
  }
  function loadProjects(){ if (!token) return; setText('authStatus','Loading projects...'); postJSON('/api/listProjects', { token: token }).then(function(res){ populateProjects(res.projects||[]); setText('authStatus','Projects loaded'); }).catch(function(err){ setText('authStatus','Projects load failed: '+err.message); }); }
  function populateProjects(list){ var sel=$('projectSelect'); if (!sel) return; sel.innerHTML=''; list.forEach(function(p){ var o=document.createElement('option'); o.value=p.id; o.textContent=p.name; sel.appendChild(o); }); if (list.length){ $('projectId').value=list[0].id; loadAccounts(list[0].id);} }
  $('projectSelect').addEventListener('change', function(){ var pid=this.value; $('projectId').value=pid; if(pid) loadAccounts(pid); });

  function yyyyMmDd(d){ return new Date(d.getTime()-d.getTimezoneOffset()*60000).toISOString().slice(0,10); }
  function setCurrentMonthDefaults(){
    var now = new Date(); var first = new Date(now.getFullYear(), now.getMonth(), 1); var last = new Date(now.getFullYear(), now.getMonth()+1, 0);
    var fromEl = $('repFrom'), toEl = $('repTo'); if (fromEl && toEl){ fromEl.value = yyyyMmDd(first); toEl.value = yyyyMmDd(last); }
  }
  setCurrentMonthDefaults();

  function populateAccountDatalist(){
    var dl = $('accountsDatalist'); if (!dl) return; dl.innerHTML='';
    accounts.forEach(function(a){ var opt=document.createElement('option'); opt.value=a.code+' · '+a.name; dl.appendChild(opt); });
  }

  $('btnRunReport') && $('btnRunReport').addEventListener('click', function(){
    if (!token) { setText('reportStatus','Please login first'); return; }
    var projectId=$('projectId').value.trim(); if (!projectId){ setText('reportStatus','Project ID required'); return; }
    var kind = $('reportKind').value; var from = $('repFrom').value; var to = $('repTo').value;
    setText('reportStatus','Running '+kind+'...');
    if (kind === 'trialBalance'){
      postJSON('/api/voiceTrialBalance', { token: token, project_id: projectId, from: from, to: to })
        .then(renderTrialBalance).catch(function(err){ setText('reportStatus','Error: '+err.message); });
    } else if (kind === 'profitAndLoss'){
      postJSON('/api/voiceProfitAndLoss', { token: token, project_id: projectId, from: from, to: to })
        .then(renderProfitAndLoss).catch(function(err){ setText('reportStatus','Error: '+err.message); });
    } else if (kind === 'balanceSheet'){
      postJSON('/api/voiceBalanceSheet', { token: token, project_id: projectId, as_of: to })
        .then(renderBalanceSheet).catch(function(err){ setText('reportStatus','Error: '+err.message); });
    } else if (kind === 'ledger'){
      var sel = $('ledgerAccount').value.trim(); var acctCode = null;
      if (/^[0-9]{3,}/.test(sel)) acctCode = sel.split(/\D/)[0];
      else { var nm = sel.replace(/^[0-9]+\s*[·\-]\s*/,''); acctCode = codeByName[normalizeName(nm)] || null; }
      if (!acctCode){ setText('reportStatus','Pick an account for ledger'); return; }
      postJSON('/api/voiceAccountLedger', { token: token, project_id: projectId, account_code: acctCode, from: from, to: to })
        .then(function(res){ renderLedger(res, acctCode); }).catch(function(err){ setText('reportStatus','Error: '+err.message); });
    }
  });

  function renderTrialBalance(res){
    var rows = (res && res.rows) || [];
    var root = $('reportOut'); root.innerHTML='';
    if (!rows.length){ root.textContent='No data'; setText('reportStatus','Done'); return; }
    var tbl = document.createElement('table');
    tbl.innerHTML = '<thead><tr><th>Code</th><th>Name</th><th>Type</th><th>Debit</th><th>Credit</th><th>Balance</th></tr></thead>';
    var tb = document.createElement('tbody');
    rows.forEach(function(r){
      var tr = document.createElement('tr');
      tr.innerHTML = '<td>'+r.account_code+'</td><td>'+ (r.name||'') +'</td><td>'+ (r.type||'') +'</td><td>'+ (+r.debit||0).toFixed(2) +'</td><td>'+ (+r.credit||0).toFixed(2) +'</td><td>'+ (+r.balance||0).toFixed(2) +'</td>';
      tb.appendChild(tr);
    });
    tbl.appendChild(tb); root.appendChild(tbl); setText('reportStatus','Done');
  }

  function renderProfitAndLoss(res){
    var root = $('reportOut'); root.innerHTML='';
    var r = res || {};
    var tbl = document.createElement('table');
    tbl.innerHTML = '<tbody>'+
      '<tr><td>Revenue</td><td style="text-align:right">'+ (+r.revenue||0).toFixed(2) +'</td></tr>'+
      '<tr><td>Expense</td><td style="text-align:right">'+ (+r.expense||0).toFixed(2) +'</td></tr>'+
      '<tr><th>Net Income</th><th style="text-align:right">'+ (+r.net_income||0).toFixed(2) +'</th></tr>'+
      '</tbody>';
    var title = document.createElement('h3'); title.textContent = 'Profit & Loss';
    root.appendChild(title);
    root.appendChild(tbl); setText('reportStatus','Done');
  }

  function renderBalanceSheet(res){
    var root = $('reportOut'); root.innerHTML='';
    function section(title, rows){
      var h = document.createElement('h3'); h.textContent=title; root.appendChild(h);
      var tbl = document.createElement('table'); var tb = document.createElement('tbody');
      (rows||[]).forEach(function(r){ var tr=document.createElement('tr'); tr.innerHTML='<td>'+r.code+'</td><td>'+r.name+'</td><td style="text-align:right">'+(+r.balance||0).toFixed(2)+'</td>'; tb.appendChild(tr); });
      tbl.appendChild(tb); root.appendChild(tbl);
    }
    var title = document.createElement('h3'); title.textContent = 'Balance Sheet'; root.appendChild(title);
    section('Assets', res.assets);
    section('Liabilities', res.liabilities);
    section('Equity', res.equity);
    setText('reportStatus','Done');
  }

  function renderLedger(res, acctCode){
    var rows = (res && res.ledger) || [];
    var root = $('reportOut'); root.innerHTML='';
    var h = document.createElement('h3'); h.textContent = 'Ledger for '+acctCode; root.appendChild(h);
    if (!rows.length){ root.appendChild(document.createTextNode('No data')); setText('reportStatus','Done'); return; }
    var tbl = document.createElement('table');
    tbl.innerHTML = '<thead><tr><th>Date</th><th>Memo</th><th>Debit</th><th>Credit</th><th>Balance</th></tr></thead>';
    var tb = document.createElement('tbody');
    rows.forEach(function(r){
      var tr = document.createElement('tr');
      tr.innerHTML = '<td>'+ (r.date||'') +'</td><td>'+ (r.memo||'') +'</td><td>'+ (+r.debit||0).toFixed(2) +'</td><td>'+ (+r.credit||0).toFixed(2) +'</td><td>'+ (+r.balance||0).toFixed(2) +'</td>';
      tb.appendChild(tr);
    });
    tbl.appendChild(tb); root.appendChild(tbl); setText('reportStatus','Done');
  }
})();

  var token = null;
  var accounts = [];
  var accountByCode = {};
  var codeByName = {};

  function normalizeName(s){
    return (s||'').toLowerCase().replace(/[^a-z0-9 ]+/g,'').replace(/\s+/g,' ').trim();
  }

  function indexAccounts(list){
    accounts = list || [];
    accountByCode = {}; codeByName = {};
    for (var i=0;i<accounts.length;i++){
      var a = accounts[i];
      accountByCode[a.code] = a;
      codeByName[normalizeName(a.name)] = a.code;
    }
  }

  function postJSON(path, obj){
    var headers = { 'Content-Type':'application/json' }; if (token && path !== '/api/login' && path !== '/api/signup') headers['Authorization'] = 'Bearer ' + token; return fetch(path, { method:'POST', headers: headers, body: JSON.stringify(obj) })
      .then(function(r){ if(!r.ok) throw new Error('HTTP '+r.status); return r.json(); });
  }

  // Auth
  $('btnLogin').addEventListener('click', function(){
    var u=$('username').value.trim(); var p=$('password').value;
    setText('authStatus','Logging in...');
    postJSON('/api/login', { username:u, password:p })
      .then(function(res){ token = res.token; setText('authStatus','Logged in'); loadProjects(); })
      .catch(function(err){ setText('authStatus','Login failed: '+err.message); });
  });
  $('btnSignup').addEventListener('click', function(){
    var u=$('username').value.trim(); var p=$('password').value;
    setText('authStatus','Signing up...');
    postJSON('/api/signup', { username:u, password:p })
      .then(function(){ setText('authStatus','Signed up. Now login.'); })
      .catch(function(err){ setText('authStatus','Signup failed: '+err.message); });
  });

  // Voice recognition (uses browser SpeechRecognition; for Google Cloud STT, integrate via proxy)
  var SR = window.SpeechRecognition || window.webkitSpeechRecognition;
  var rec = SR ? new SR() : null;
  if (rec) {
    rec.lang = 'en-US'; rec.interimResults = true; rec.continuous = false;
    rec.onresult = function(e){
      var full=''; var finalText='';
      for (var i=e.resultIndex; i<e.results.length; i++){
        var seg=e.results[i][0].transcript; full+=seg; if(e.results[i].isFinal) finalText+=seg;
      }
      var t=(finalText||full).trim(); $('transcript').value=t;
      if (finalText){ currentLines=(parseLinesFromTranscript(t)); 
	                  setText('parseStatus', currentLines.length? ('Parsed '+currentLines.length+' line(s)') : 'No lines recognized');
					  renderLines(currentLines); }
    };
    rec.onstart = function(){ setText('voiceStatus','Listening...'); $('btnVoice').textContent='Stop Voice'; };
    rec.onend = function(){ setText('voiceStatus',''); $('btnVoice').textContent='Start Voice'; };
    rec.onerror = function(e){ setText('voiceStatus','Error: '+e.error); };
  } else {
    setText('voiceStatus','SpeechRecognition not supported in this browser');
  }

  $('btnVoice').addEventListener('click', function(){
    if (!rec) return;
    try {
      if ($('btnVoice').textContent.indexOf('Start')===0) rec.start(); else rec.stop();
    } catch (e) { setText('voiceStatus','Error: '+e.message); }
  });

  // Parse transcript into lines array
function parseLinesFromTranscript(t){
  var out=[]; if (!t) return out;

  // normalize
  var s=t.toLowerCase();
  s = s.replace(/\./g,' and ')
       .replace(/,/g,' and ')
       .replace(/\s+/g,' ')
       .trim();

  // split into parts by "and"
  var parts = s.split(/\band\b/);

  // helper: resolve spoken account (name or code) to account_code
  function resolveAccount(acctRaw){
    var a = (acctRaw||'').trim();
    if (!a) return null;
    if (/^[0-9]{3,}$/.test(a)) return a.toUpperCase();
    var key = normalizeName(a);
    if (codeByName[key]) return codeByName[key];
    // fallback: partial contains
    var best=null;
    for (var nm in codeByName){
      if (nm.indexOf(key)!==-1){ best = codeByName[nm]; break; }
    }
    return best;
  }

  for (var i=0;i<parts.length;i++){
    var p = parts[i].trim();
    if (!p) continue;

    var m, amt, acctRaw, acctCode, kind;

    // NEW FORMAT A:
    // "pay <acctA> <amount> from <acctB>"
    // => debit acctA, credit acctB
    m = p.match(/\bpay\s+([a-z0-9 _\-]{2,})\s+([0-9]+(?:\.[0-9]+)?)\s+from\s+([a-z0-9 _\-]{2,})$/);
    if (m){
      var acctARaw = m[1], amountA = parseFloat(m[2]), acctBRaw = m[3];
      var acctA = resolveAccount(acctARaw), acctB = resolveAccount(acctBRaw);
      if (acctA){ out.push({ account_code: acctA, debit: amountA, credit: 0 }); }
      if (acctB){ out.push({ account_code: acctB, debit: 0, credit: amountA }); }
      continue;
    }

    // NEW FORMAT B:
    // "credit <acctA> <amount> from <acctB>"
    // => credit acctA, debit acctB
    m = p.match(/\bcredit\s+([a-z0-9 _\-]{2,})\s+([0-9]+(?:\.[0-9]+)?)\s+from\s+([a-z0-9 _\-]{2,})$/);
    if (m){
      var acctARawC = m[1], amountC = parseFloat(m[2]), acctBRawC = m[3];
      var acctAC = resolveAccount(acctARawC), acctBC = resolveAccount(acctBRawC);
      if (acctAC){ out.push({ account_code: acctAC, debit: 0, credit: amountC }); }
      if (acctBC){ out.push({ account_code: acctBC, debit: amountC, credit: 0 }); }
      continue;
    }

    // (Optional symmetry) "debit <acctA> <amount> from <acctB>"
    // => debit acctA, credit acctB
    m = p.match(/\bdebit\s+([a-z0-9 _\-]{2,})\s+([0-9]+(?:\.[0-9]+)?)\s+from\s+([a-z0-9 _\-]{2,})$/);
    if (m){
      var acctARawD = m[1], amountD = parseFloat(m[2]), acctBRawD = m[3];
      var acctAD = resolveAccount(acctARawD), acctBD = resolveAccount(acctBRawD);
      if (acctAD){ out.push({ account_code: acctAD, debit: amountD, credit: 0 }); }
      if (acctBD){ out.push({ account_code: acctBD, debit: 0, credit: amountD }); }
      continue;
    }

    // BACK-COMPAT:
    // "debit|credit|pay <amount> to/into/on/in/for <account>"
    m = p.match(/\b(debit|credit|pay)\s+([0-9]+(?:\.[0-9]+)?)\s+(?:to|into|on|in|for)\s+([a-z0-9 _\-]{2,})$/);
    if (m){
      kind = (m[1]==='pay' ? 'debit' : m[1]);
      amt = parseFloat(m[2]);
      acctRaw = (m[3]||'').trim();
      acctCode = resolveAccount(acctRaw);
      if (acctCode){
        out.push({
          account_code: acctCode,
          debit:  kind==='debit' ? amt : 0,
          credit: kind==='credit'? amt : 0
        });
      }
      continue;
    }

    // else: no match for this part -> skip
  }

  return out;
}


  function renderLines(lines){
    var root=$('lines'); root.innerHTML='';
    if (!lines.length){ root.innerHTML='<div class="status">No lines parsed yet.</div>'; return; }
    for (var i=0;i<lines.length;i++){
      var l=lines[i];
      var row=document.createElement('div'); row.className='line';
      row.innerHTML=
        '<label>Account <input value="'+(l.account_code||'')+'" data-k="account_code"/></label>'+
        '<label>Debit <input value="'+(l.debit||0)+'" data-k="debit" type="number" step="0.01"/></label>'+
        '<label>Credit <input value="'+(l.credit||0)+'" data-k="credit" type="number" step="0.01"/></label>'+
        '<button data-act="del">✕</button>';
      row.addEventListener('input', function(ev){
        var k=ev.target.getAttribute('data-k'); if(!k) return;
        var idx=[].indexOf.call(root.children, this); if(idx<0) return;
        if (k==='account_code') lines[idx][k]=ev.target.value.toUpperCase();
        else lines[idx][k]=parseFloat(ev.target.value||0);
      });
      row.querySelector('button[data-act="del"]').addEventListener('click', (function(idx){ return function(){ lines.splice(idx,1); renderLines(lines); };})(i));
      root.appendChild(row);
    }
  }

  var currentLines=[];

  $('btnParse').addEventListener('click', function(){
    var t=$('transcript').value;
    currentLines = (parseLinesFromTranscript(t));
    if (!currentLines.length) setText('parseStatus','No lines recognized. Try: "debit 120 to 5300 and credit 120 to 1000"');
    else setText('parseStatus','Parsed '+currentLines.length+' line(s)');
    renderLines(currentLines);
  });

  $('btnSubmit').addEventListener('click', function(){
    if (!token) { setText('submitStatus','Please login first'); return; }
    var projectId=$('projectId').value.trim();
    if (!projectId){ setText('submitStatus','Project ID required'); return; }
    var date=$('date').value; var memo=$('memo').value||'';
    if (!currentLines.length){ setText('submitStatus','No lines to submit'); return; }
    setText('submitStatus','Posting...');
    postJSON('/api/postJournal', { token: token, project_id: projectId, date: date, memo: memo, lines: currentLines })
      .then(function(res){ setText('submitStatus','Posted entry '+(res.entry && res.entry.id ? res.entry.id : 'OK')); currentLines=[]; renderLines(currentLines); })
      .catch(function(err){ setText('submitStatus','Post failed: '+err.message); });
  });


  function loadAccounts(projectId){
    if (!token || !projectId) return Promise.resolve();
    setText('parseStatus','Loading accounts...');
    return postJSON('/api/listAccounts', { token: token, project_id: projectId })
      .then(function(res){ indexAccounts(res.accounts||[]); setText('parseStatus','Accounts loaded: '+accounts.length); populateAccountDatalist(); })
      .catch(function(err){ setText('parseStatus','Accounts load failed: '+err.message); });
  }
  function loadProjects(){ if (!token) return; setText('authStatus','Loading projects...'); postJSON('/api/listProjects', { token: token }).then(function(res){ populateProjects(res.projects||[]); setText('authStatus','Projects loaded'); }).catch(function(err){ setText('authStatus','Projects load failed: '+err.message); }); }
  function populateProjects(list){ var sel=$('projectSelect'); if (!sel) return; sel.innerHTML=''; list.forEach(function(p){ var o=document.createElement('option'); o.value=p.id; o.textContent=p.name; sel.appendChild(o); }); if (list.length){ $('projectId').value=list[0].id; loadAccounts(list[0].id);} }
  $('projectSelect').addEventListener('change', function(){ var pid=this.value; $('projectId').value=pid; if(pid) loadAccounts(pid); });

  function yyyyMmDd(d){ return new Date(d.getTime()-d.getTimezoneOffset()*60000).toISOString().slice(0,10); }
  function setCurrentMonthDefaults(){
    var now = new Date(); var first = new Date(now.getFullYear(), now.getMonth(), 1); var last = new Date(now.getFullYear(), now.getMonth()+1, 0);
    var fromEl = $('repFrom'), toEl = $('repTo'); if (fromEl && toEl){ fromEl.value = yyyyMmDd(first); toEl.value = yyyyMmDd(last); }
  }
  setCurrentMonthDefaults();

  function populateAccountDatalist(){
    var dl = $('accountsDatalist'); if (!dl) return; dl.innerHTML='';
    accounts.forEach(function(a){ var opt=document.createElement('option'); opt.value=a.code+' · '+a.name; dl.appendChild(opt); });
  }

  $('btnRunReport') && $('btnRunReport').addEventListener('click', function(){
    if (!token) { setText('reportStatus','Please login first'); return; }
    var projectId=$('projectId').value.trim(); if (!projectId){ setText('reportStatus','Project ID required'); return; }
    var kind = $('reportKind').value; var from = $('repFrom').value; var to = $('repTo').value;
    setText('reportStatus','Running '+kind+'...');
    if (kind === 'trialBalance'){
      postJSON('/api/voiceTrialBalance', { token: token, project_id: projectId, from: from, to: to })
        .then(renderTrialBalance).catch(function(err){ setText('reportStatus','Error: '+err.message); });
    } else if (kind === 'profitAndLoss'){
      postJSON('/api/voiceProfitAndLoss', { token: token, project_id: projectId, from: from, to: to })
        .then(renderProfitAndLoss).catch(function(err){ setText('reportStatus','Error: '+err.message); });
    } else if (kind === 'balanceSheet'){
      postJSON('/api/voiceBalanceSheet', { token: token, project_id: projectId, as_of: to })
        .then(renderBalanceSheet).catch(function(err){ setText('reportStatus','Error: '+err.message); });
    } else if (kind === 'ledger'){
      var sel = $('ledgerAccount').value.trim(); var acctCode = null;
      if (/^[0-9]{3,}/.test(sel)) acctCode = sel.split(/\D/)[0];
      else { var nm = sel.replace(/^[0-9]+\s*[·\-]\s*/,''); acctCode = codeByName[normalizeName(nm)] || null; }
      if (!acctCode){ setText('reportStatus','Pick an account for ledger'); return; }
      postJSON('/api/voiceAccountLedger', { token: token, project_id: projectId, account_code: acctCode, from: from, to: to })
        .then(function(res){ renderLedger(res, acctCode); }).catch(function(err){ setText('reportStatus','Error: '+err.message); });
    }
  });

  function renderTrialBalance(res){
    var rows = (res && res.rows) || [];
    var root = $('reportOut'); root.innerHTML='';
    if (!rows.length){ root.textContent='No data'; setText('reportStatus','Done'); return; }
    var tbl = document.createElement('table');
    tbl.innerHTML = '<thead><tr><th>Code</th><th>Name</th><th>Type</th><th>Debit</th><th>Credit</th><th>Balance</th></tr></thead>';
    var tb = document.createElement('tbody');
    rows.forEach(function(r){
      var tr = document.createElement('tr');
      tr.innerHTML = '<td>'+r.account_code+'</td><td>'+ (r.name||'') +'</td><td>'+ (r.type||'') +'</td><td>'+ (+r.debit||0).toFixed(2) +'</td><td>'+ (+r.credit||0).toFixed(2) +'</td><td>'+ (+r.balance||0).toFixed(2) +'</td>';
      tb.appendChild(tr);
    });
    tbl.appendChild(tb); root.appendChild(tbl); setText('reportStatus','Done');
  }

  function renderProfitAndLoss(res){
    var root = $('reportOut'); root.innerHTML='';
    var r = res || {};
    var tbl = document.createElement('table');
    tbl.innerHTML = '<tbody>'+
      '<tr><td>Revenue</td><td style="text-align:right">'+ (+r.revenue||0).toFixed(2) +'</td></tr>'+
      '<tr><td>Expense</td><td style="text-align:right">'+ (+r.expense||0).toFixed(2) +'</td></tr>'+
      '<tr><th>Net Income</th><th style="text-align:right">'+ (+r.net_income||0).toFixed(2) +'</th></tr>'+
      '</tbody>';
    var title = document.createElement('h3'); title.textContent = 'Profit & Loss';
    root.appendChild(title);
    root.appendChild(tbl); setText('reportStatus','Done');
  }

  function renderBalanceSheet(res){
    var root = $('reportOut'); root.innerHTML='';
    function section(title, rows){
      var h = document.createElement('h3'); h.textContent=title; root.appendChild(h);
      var tbl = document.createElement('table'); var tb = document.createElement('tbody');
      (rows||[]).forEach(function(r){ var tr=document.createElement('tr'); tr.innerHTML='<td>'+r.code+'</td><td>'+r.name+'</td><td style="text-align:right">'+(+r.balance||0).toFixed(2)+'</td>'; tb.appendChild(tr); });
      tbl.appendChild(tb); root.appendChild(tbl);
    }
    var title = document.createElement('h3'); title.textContent = 'Balance Sheet'; root.appendChild(title);
    section('Assets', res.assets);
    section('Liabilities', res.liabilities);
    section('Equity', res.equity);
    setText('reportStatus','Done');
  }

  function renderLedger(res, acctCode){
    var rows = (res && res.ledger) || [];
    var root = $('reportOut'); root.innerHTML='';
    var h = document.createElement('h3'); h.textContent = 'Ledger for '+acctCode; root.appendChild(h);
    if (!rows.length){ root.appendChild(document.createTextNode('No data')); setText('reportStatus','Done'); return; }
    var tbl = document.createElement('table');
    tbl.innerHTML = '<thead><tr><th>Date</th><th>Memo</th><th>Debit</th><th>Credit</th><th>Balance</th></tr></thead>';
    var tb = document.createElement('tbody');
    rows.forEach(function(r){
      var tr = document.createElement('tr');
      tr.innerHTML = '<td>'+ (r.date||'') +'</td><td>'+ (r.memo||'') +'</td><td>'+ (+r.debit||0).toFixed(2) +'</td><td>'+ (+r.credit||0).toFixed(2) +'</td><td>'+ (+r.balance||0).toFixed(2) +'</td>';
      tb.appendChild(tr);
    });
    tbl.appendChild(tb); root.appendChild(tbl); setText('reportStatus','Done');
  }
})();

