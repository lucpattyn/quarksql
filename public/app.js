// public/app.js - Vanilla JS SPA for Quarksql Accounting (UTF-8 clean)

const API = {
  async call(name, payload){
    const headers = { 'Content-Type': 'application/json' };
    if (store.token) headers['Authorization'] = 'Bearer ' + store.token;
    const res = await fetch('/api/' + name, { method:'POST', headers, body: JSON.stringify(payload||{}) });
    const text = await res.text();
    let data; try { data = text ? JSON.parse(text) : {}; } catch { data = { error: text || res.statusText }; }
    if (!res.ok || data.error) throw new Error(data.error || res.statusText);
    return data;
  }
};

const store = {
  token: localStorage.getItem('token') || '',
  projects: [],
  accounts: [],
  entries: [],
  currentProject: '',
};

function setToken(t){ store.token = t; localStorage.setItem('token', t); }
function fmt(n){ return (+n).toFixed(2); }

function nav(){
  const nav = document.getElementById('nav');
  nav.innerHTML = '';
  const links = [];
  if (!store.token) {
    links.push(['Login','login'],['Sign Up','signup']);
  } else {
    links.push(['Dashboard','dashboard'],['Accounts','accounts'],['Journal','journal'],['Reports','reports']);
    const logout = document.createElement('a');
    logout.textContent = 'Logout';
    logout.href = '#';
    logout.onclick = (e)=>{ e.preventDefault(); setToken(''); location.hash = '#login'; render(); };
    nav.appendChild(logout);
  }
  links.forEach(([label,hash])=>{
    const a = document.createElement('a');
    a.textContent = label;
    a.href = '#' + hash;
    if (location.hash === '#' + hash) a.classList.add('active');
    nav.appendChild(a);
  });
}

function view(){ return document.getElementById('view'); }

function el(tag, attrs={}, children=[]){
  const e = document.createElement(tag);
  Object.entries(attrs).forEach(([k,v])=>{
    if (k==='class') e.className = v; else if (k==='html') e.innerHTML=v; else e[k]=v;
  });
  (children||[]).forEach(c => e.appendChild(typeof c==='string'?document.createTextNode(c):c));
  return e;
}

async function ensureAuthed(){
  if (!store.token) { location.hash = '#login'; render(); throw new Error('not authed'); }
  try { await API.call('verify', { token: store.token }); } catch(e){ setToken(''); location.hash = '#login'; render(); throw e; }
}

async function page_login(){
  const v = view();
  v.innerHTML = '';
  v.appendChild(el('div',{class:'grid cols-2'},
    [
      el('div',{class:'card'},[
        el('h2',{html:'Login'}),
        el('input',{class:'input',placeholder:'Username',id:'login_user'}),
        el('input',{class:'input',placeholder:'Password',type:'password',id:'login_pass',style:'margin-top:8px'}),
        el('button',{class:'btn',style:'margin-top:10px', onclick: async ()=>{
          const username = document.getElementById('login_user').value.trim();
          const password = document.getElementById('login_pass').value;
          try {
            const res = await API.call('login', { username, password });
            setToken(res.token);
            location.hash = '#dashboard'; render();
          } catch(e){ alert(e.message); }
        }},['Login'])
      ]),
      el('div',{class:'card'},[
        el('h2',{html:'Sign Up'}),
        el('input',{class:'input',placeholder:'Username',id:'su_user'}),
        el('input',{class:'input',placeholder:'Password',type:'password',id:'su_pass',style:'margin-top:8px'}),
        el('button',{class:'btn',style:'margin-top:10px', onclick: async ()=>{
          const username = document.getElementById('su_user').value.trim();
          const password = document.getElementById('su_pass').value;
          try { await API.call('signup', { username, password }); alert('Account created. Please login.'); }
          catch(e){ alert(e.message); }
        }},['Create Account'])
      ])
    ]
  ));
}

async function refreshProjects(){
  const res = await API.call('listProjects',{ token: store.token });
  store.projects = res.projects || [];
  if (!store.currentProject && store.projects.length) store.currentProject = store.projects[0].id;
}

function projectSelector(onChange){
  const sel = el('select',{class:'input'});
  store.projects.forEach(p => {
    const o = el('option', {value:p.id}); o.textContent = p.name; if (p.id===store.currentProject) o.selected = true; sel.appendChild(o);
  });
  sel.onchange = (e)=>{ store.currentProject = sel.value; onChange && onChange(sel.value); };
  return sel;
}

async function page_dashboard(){
  await ensureAuthed();
  await refreshProjects();
  const v = view(); v.innerHTML = '';
  const createCard = el('div',{class:'card'},[
    el('h2',{html:'Create Project'}),
    el('input',{class:'input',placeholder:'Project name',id:'proj_name'}),
    el('button',{class:'btn',style:'margin-top:10px', onclick: async ()=>{
      const name = document.getElementById('proj_name').value.trim();
      if (!name) return alert('Enter a project name');
      try { await API.call('createProject',{ token: store.token, name }); await refreshProjects(); render(); } catch(e){ alert(e.message); }
    }},['Create'])
  ]);

  const projectsCard = el('div',{class:'card'},[
    el('h2',{html:'Your Projects'}),
    el('div',{},[projectSelector(()=>render())])
  ]);

  v.appendChild(el('div',{class:'grid cols-2'},[createCard, projectsCard]));
}

async function page_accounts(){
  await ensureAuthed();
  await refreshProjects();
  if (!store.currentProject) return page_dashboard();
  const v = view(); v.innerHTML = '';

  const header = el('div',{class:'card'},[
    el('h2',{html:'Accounts'}),
    projectSelector(async ()=>{ await page_accounts(); }),
    el('div',{style:'height:8px'}),
    el('button',{class:'btn ghost', onclick: async ()=>{
      try { await API.call('seedAccountsGlobal',{ token: store.token }); alert('Global template ensured. New projects will auto-seed.'); } catch(e){ alert(e.message); }
    }},['Ensure Global Template'])
  ]);

  const tableCard = el('div',{class:'card'},[
    el('h2',{html:'Chart of Accounts'}),
    el('table',{class:'table',id:'acct_table'})
  ]);

  const formCard = el('div',{class:'card'},[
    el('h2',{html:'Add Account'}),
    el('div',{class:'grid cols-3'},[
      el('input',{class:'input',placeholder:'Code',id:'acc_code'}),
      el('input',{class:'input',placeholder:'Name',id:'acc_name'}),
      (function(){
        const sel = el('select',{class:'input',id:'acc_type'});
        ['Asset','Liability','Equity','Revenue','Expense'].forEach(t=>{ const o = el('option',{value:t}); o.textContent=t; sel.appendChild(o); });
        return sel;
      })()
    ]),
    el('button',{class:'btn',style:'margin-top:10px', onclick: async ()=>{
      const code = document.getElementById('acc_code').value.trim();
      const name = document.getElementById('acc_name').value.trim();
      const type = document.getElementById('acc_type').value;
      if (!code || !name) return alert('Fill all fields');
      try { await API.call('addAccount',{ token: store.token, project_id: store.currentProject, code, name, type }); await page_accounts(); } catch(e){ alert(e.message); }
    }},['Add Account'])
  ]);

  v.appendChild(el('div',{class:'grid cols-2'},[header, formCard]));
  v.appendChild(el('div',{style:'height:12px'}));
  v.appendChild(tableCard);

  const res = await API.call('listAccounts',{ token: store.token, project_id: store.currentProject });
  const rows = res.accounts || [];
  const table = document.getElementById('acct_table');
  table.innerHTML = '<tr><th>Code</th><th>Name</th><th>Type</th><th>Status</th></tr>' + rows.map(r=>`<tr data-code="${r.code}" class="clickable"><td><a href="#ledger:${r.code}">${r.code}</a></td><td>${r.name}</td><td>${r.type}</td><td>${r.is_active?'Active':'Inactive'}</td></tr>`).join('');
}

async function page_journal(){
  await ensureAuthed();
  await refreshProjects();
  if (!store.currentProject) return page_dashboard();
  const v = view(); v.innerHTML = '';

  // Header + form controls
  const header = el('div',{class:'card'},[
    el('h2',{html:'New Journal Entry'}),
    projectSelector(()=>page_journal()),
    el('div',{class:'grid cols-3',style:'margin-top:8px'},[
      el('input',{class:'input',type:'date',id:'je_date', value: new Date().toISOString().slice(0,10)}),
      el('input',{class:'input',placeholder:'Memo',id:'je_memo'}),
      el('button',{class:'btn', style:'background:#0d6efd;color:#fff;', 
	  	onclick: addLine, html:'<i class="fa-solid fa-plus"></i> Add Line'})

    ]),
    el('div',{id:'lines'}),
    el('button',{class:'btn',style:'margin-top:10px', onclick: postEntry, html:'<i class="fa-solid fa-paper-plane"></i> Post Entry'})
  ]);

  const listCard = el('div',{class:'card'},[
    el('h2',{html:'Recent Entries'}),
    el('table',{class:'table',id:'je_table'})
  ]);

  v.appendChild(el('div',{class:'grid cols-2'},[header, listCard]));

  // Lines state
  const resAcc = await API.call('listAccounts',{ token: store.token, project_id: store.currentProject });
  const accounts = resAcc.accounts || [];
  const linesDiv = document.getElementById('lines');
  let lines = [];

  // --- compact row UI ---
  // 4 columns: Account | Debit | Credit | [ � ]
  // Use inline styles to make inputs shorter without touching your global CSS.
  function lineRow(idx){
    const row = el('div',{
      class:'je-row',
      style:'display:grid;grid-template-columns:2fr 1fr 1fr auto;gap:8px;align-items:center;margin:6px 0;'
    });

    const sel = el('select',{
      class:'input',
      style:'height:36px;padding:6px 10px;'
    });
    accounts.forEach(a=>{
      const o = el('option',{value:a.code}); o.textContent = `${a.code} - ${a.name}`; sel.appendChild(o);
    });

    const debit  = el('input',{class:'input',placeholder:'Debit', type:'number', step:'0.01', style:'height:36px;padding:6px 10px;'});
    const credit = el('input',{class:'input',placeholder:'Credit',type:'number', step:'0.01', style:'height:36px;padding:6px 10px;'});

    // Small icon-only remove button
    const rm = el('button',{
	  title:'Remove line',
	  style:'height:36px;width:36px;display:flex;align-items:center;justify-content:center;border:none;border-radius:8px;background:#dc3545;color:#fff;cursor:pointer;',
	  onclick:()=>{ lines.splice(idx,1); redraw(); },
	  html:'<i class="fa-solid fa-xmark"></i>'
	});
    
    row.appendChild(sel);
    row.appendChild(debit);
    row.appendChild(credit);
    row.appendChild(rm);
    return row;
  }

  function redraw(){
    linesDiv.innerHTML = '';
    lines.forEach((_,i)=> linesDiv.appendChild(lineRow(i)));
  }

  function addLine(){ lines.push({}); redraw(); }

  async function postEntry(){
    const date = document.getElementById('je_date').value;
    const memo = document.getElementById('je_memo').value;

    // Collect rows
    const ui = Array.from(linesDiv.querySelectorAll('.je-row'));
    const payloadLines = ui.map(row => {
      const [sel,debit,credit] = row.children; // rm is the 4th child
      return { account_code: sel.value, debit: +debit.value || 0, credit: +credit.value || 0 };
    });

    try {
      await API.call('postJournal',{ token: store.token, project_id: store.currentProject, date, memo, lines: payloadLines });
      alert('Posted.');
      page_journal();
    } catch(e){ alert(e.message); }
  }

  // Start with two lines
  addLine(); addLine();

  // Recent entries
  const res = await API.call('listJournals',{ token: store.token, project_id: store.currentProject, limit: 50 });
  const rows = res.entries || [];
  const table = document.getElementById('je_table');
  table.innerHTML = '<tr><th>Date</th><th>Memo</th><th>By</th><th>Actions</th></tr>' +
    rows.map(r=>`<tr data-id="${r.id}"><td>${r.date}</td><td>${r.memo||''}</td><td>${r.created_by}</td><td><button class="btn ghost" data-view="${r.id}"><i class="fa-regular fa-eye"></i> View</button></td></tr>`).join('');
  table.querySelectorAll('button[data-view]').forEach(btn=>{
    btn.onclick = async ()=>{
      const id = btn.getAttribute('data-view');
      const detail = await API.call('getJournal',{ token: store.token, entry_id: id });
      const lines = detail.lines||[];
      const detailRow = document.createElement('tr');
      const td = document.createElement('td');
      td.colSpan = 4;
      td.innerHTML = `<table class="table"><tr><th>#</th><th>Account</th><th>Debit</th><th>Credit</th></tr>${lines.map(l=>`<tr><td>${l.line_no}</td><td><a href="#ledger:${l.account_code}">${l.account_code}</a></td><td>${fmt(l.debit||0)}</td><td>${fmt(l.credit||0)}</td></tr>`).join('')}</table>`;
      detailRow.appendChild(td);
      btn.closest('tr').after(detailRow);
    };
  });
}


async function page_reports(){
  await ensureAuthed();
  await refreshProjects();
  if (!store.currentProject) return page_dashboard();
  const v = view(); v.innerHTML = '';

  const head = el('div',{class:'card'},[
    el('h2',{html:'Reports'}),
    projectSelector(()=>page_reports()),
    el('div',{class:'tabs'},[
      el('div',{class:'tab active',id:'tab-tb'},['Trial Balance']),
      el('div',{class:'tab',id:'tab-bs'},['Balance Sheet']),
      el('div',{class:'tab',id:'tab-pl'},['Profit & Loss']),
      el('div',{class:'tab',id:'tab-overall'},['Overall (All Projects)']),
    ])
  ]);

  const content = el('div',{class:'card',id:'report_content'},[]);

  v.appendChild(head); v.appendChild(el('div',{style:'height:10px'})); v.appendChild(content);

  function activate(id){
    Array.from(document.querySelectorAll('.tab')).forEach(t=>t.classList.remove('active'));
    document.getElementById(id).classList.add('active');
  }

  document.getElementById('tab-tb').onclick = async ()=>{
    activate('tab-tb');
    content.innerHTML = '<p>Loading trial balance…</p>';
    try {
      const r = await API.call('trialBalance',{ token: store.token, project_id: store.currentProject });
      const rows = r.rows||[];
      content.innerHTML = `<h3>Trial Balance</h3>
        <table class="table"><tr><th>Code</th><th>Name</th><th>Type</th><th>Debit</th><th>Credit</th><th>Balance</th></tr>
        ${rows.map(x=>`<tr><td>${x.account_code}</td><td>${x.name}</td><td>${x.type}</td><td>${fmt(x.debit||0)}</td><td>${fmt(x.credit||0)}</td><td>${fmt(x.balance||0)}</td></tr>`).join('')}</table>`;
    } catch(e) {
      content.innerHTML = `<div class="card"><h3>Error</h3><p>${e.message}</p></div>`;
    }
  };

  document.getElementById('tab-bs').onclick = async ()=>{
    activate('tab-bs');
    content.innerHTML = '<p>Loading balance sheet…</p>';
    try {
      const r = await API.call('balanceSheet',{ token: store.token, project_id: store.currentProject });
      function section(name, arr){ return `<h4>${name}</h4><table class="table"><tr><th>Code</th><th>Name</th><th>Balance</th></tr>${(arr||[]).map(x=>`<tr><td>${x.code}</td><td>${x.name}</td><td>${fmt(x.balance)}</td></tr>`).join('')}</table>`; }
      const totals = r.totals || { assets:0, liabilities:0, equity:0 };
      content.innerHTML = `<h3>Balance Sheet</h3>
        ${section('Assets', r.assets)} ${section('Liabilities', r.liabilities)} ${section('Equity', r.equity)}
        <p><b>Totals:</b> Assets ${fmt(totals.assets)} - Liabilities ${fmt(totals.liabilities)} - Equity ${fmt(totals.equity)}</p>`;
    } catch(e) {
      content.innerHTML = `<div class="card"><h3>Error</h3><p>${e.message}</p></div>`;
    }
  };

  document.getElementById('tab-pl').onclick = async ()=>{
    activate('tab-pl');
    content.innerHTML = '<p>Loading profit & loss…</p>';
    try {
      const r = await API.call('profitAndLoss',{ token: store.token, project_id: store.currentProject });
      content.innerHTML = `<h3>Profit & Loss</h3>
        <div class="grid cols-3"><div class="card"><h2>Revenue</h2><p>${fmt(r.revenue)}</p></div><div class="card"><h2>Expense</h2><p>${fmt(r.expense)}</p></div><div class="card"><h2>Net Income</h2><p>${fmt(r.net_income)}</p></div></div>`;
    } catch(e) {
      content.innerHTML = `<div class="card"><h3>Error</h3><p>${e.message}</p></div>`;
    }
  };

  document.getElementById('tab-overall').onclick = async ()=>{
    activate('tab-overall');
    content.innerHTML = '<p>Loading overall trial balance…</p>';
    try {
      const r = await API.call('overallTrialBalance',{ token: store.token });
      const rows = r.rows||[];
      content.innerHTML = `<h3>Overall Trial Balance</h3>
        <table class="table"><tr><th>Project</th><th>Account</th><th>Debit</th><th>Credit</th></tr>
        ${rows.map(x=>`<tr><td>${x.project_id}</td><td>${x.account_id||x.account_code}</td><td>${fmt(x.debit||0)}</td><td>${fmt(x.credit||0)}</td></tr>`).join('')}</table>`;
    } catch(e) {
      content.innerHTML = `<div class="card"><h3>Error</h3><p>${e.message}</p></div>`;
    }
  };

  // Load default
  document.getElementById('tab-tb').click();
}

// Router
async function render(){
  nav();
  const hash = location.hash || '#login';
  if (hash==='#login' || !store.token) return page_login();
  if (hash.startsWith('#ledger:')) {
    const code = hash.split(':')[1];
    return page_ledger(code);
  }
  switch(hash){
    case '#dashboard': return page_dashboard();
    case '#accounts': return page_accounts();
    case '#journal': return page_journal();
    case '#reports': return page_reports();
    case '#signup': return page_login();
    default: return page_dashboard();
  }
}

window.addEventListener('hashchange', render);
render();

// Account ledger page
async function page_ledger(accountCode){
  await ensureAuthed();
  await refreshProjects();
  if (!store.currentProject) return page_dashboard();
  const v = view(); v.innerHTML = '';
  const head = el('div',{class:'card'},[
    el('h2',{html:`Ledger · ${accountCode}`}),
    projectSelector(async ()=>{ await page_ledger(accountCode); })
  ]);
  const card = el('div',{class:'card'},[
    el('h2',{html:'Transactions'}),
    el('table',{class:'table',id:'ledger_table'})
  ]);
  v.appendChild(el('div',{class:'grid cols-2'},[head, el('div',{})]));
  v.appendChild(card);
  try {
    const r = await API.call('accountLedger',{ token: store.token, project_id: store.currentProject, account_code: accountCode });
    const rows = r.ledger||[];
    const tbl = document.getElementById('ledger_table');
    tbl.innerHTML = '<tr><th>Date</th><th>Memo</th><th>Debit</th><th>Credit</th><th>Balance</th></tr>' + rows.map(x=>`<tr><td>${x.date}</td><td>${(x.memo||'')}</td><td>${fmt(x.debit||0)}</td><td>${fmt(x.credit||0)}</td><td>${fmt(x.balance||0)}</td></tr>`).join('');
  } catch(e){
    const tbl = document.getElementById('ledger_table');
    tbl.innerHTML = `<tr><td colspan="5">${e.message}</td></tr>`;
  }
}
