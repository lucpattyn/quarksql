// public/blockchain/app.js
let token = '';

// Drop-in: replace your existing apiCall with this version
async function apiCall(name, params){
  const headers = { 'Content-Type': 'application/json' };

  // Try current token var first, then fall back to localStorage
  let t = (typeof token === 'string' && token) ? token : '';
  if (!t) {
    try { t = localStorage.getItem('token') || ''; } catch(_) {}
  }

  if (t) headers['Authorization'] = 'Bearer ' + t;

  const r = await fetch(`/api/${name}`, {
    method: 'POST',
    headers,
    body: JSON.stringify(params || {})
  });

  const text = await r.text();
  let data;
  try { data = text ? JSON.parse(text) : {}; } catch { data = { error: text || r.statusText }; }

  if (!r.ok || data.error) throw new Error(data.error || r.statusText);
  return data;
}

function setAuthStatus(msg){ document.getElementById('authStatus').textContent = msg; }
function show(elId, obj){ document.getElementById(elId).textContent = JSON.stringify(obj, null, 2); }

async function signup(){
  const username = document.getElementById('u').value || 'demo';
  const password = document.getElementById('p').value || 'demo';
  try { await apiCall('signup', { username, password }); setAuthStatus('Signed up. You can now login.'); } catch(e){ setAuthStatus('Signup failed'); }
}

async function login(){
  const username = document.getElementById('u').value || 'demo';
  const password = document.getElementById('p').value || 'demo';
  try { const res = await apiCall('login', { username, password }); token = res.token || ''; setAuthStatus(token ? 'Logged in.' : 'Login failed'); } catch(e){ setAuthStatus('Login failed'); }
}

async function init(){
  if (!token) return setAuthStatus('Please login first');
  const res = await apiCall('bc_init', { token });
  document.getElementById('initResult').textContent = `Blocks: ${res.blocks}, Rules: ${res.rules}`;
}

async function postTx(){
  if (!token) return setAuthStatus('Please login first');
  const notation = document.getElementById('t_notation').value || 'Transfer to savings';
  const sourceAccount = document.getElementById('t_from').value || 'Assets:Checking';
  const destinationAccount = document.getElementById('t_to').value || 'Assets:Savings';
  const amount = parseFloat(document.getElementById('t_amount').value || '1000');
  const currency = document.getElementById('t_currency').value || 'USD';
  const res = await apiCall('bc_addTransaction', { token, notation, sourceAccount, destinationAccount, amount, currency });
  show('postResult', res);
}

async function getBal(){
  if (!token) return setAuthStatus('Please login first');
  const account = document.getElementById('b_account').value || 'Assets:Savings';
  const asOf = document.getElementById('b_asof').value || null;
  const res = await apiCall('bc_getBalance', { token, account, asOf });
  show('balResult', res);
}

async function getTx(){
  if (!token) return setAuthStatus('Please login first');
  const account = document.getElementById('x_account').value || 'Assets:Savings';
  const res = await apiCall('bc_getTransactions', { token, account });
  show('txResult', res);
}

async function listRules(){
  if (!token) return setAuthStatus('Please login first');
  const res = await apiCall('bc_listRules', { token });
  show('rulesResult', res);
}

function bind(id, fn){ document.getElementById(id).addEventListener('click', fn); }

bind('signup', signup);
bind('login', login);
bind('init', init);
bind('post', postTx);
bind('getBal', getBal);
bind('getTx', getTx);
bind('listRules', listRules);

