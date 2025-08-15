// scripts/blockchain/ledger.js
// Immutable blockchain journal + rules engine with RocksDB-backed storage via V8 db.kv*
// ES5-compatible (avoid object spread and optional chaining for older V8)

// Simple type checks
function isType(value, type) {
  if (type === 'Array') return Array.isArray(value);
  if (type === 'null') return value === null;
  if (type === 'undefined') return value === undefined;
  return typeof value === type;
}

// Amount
function createAmount(value, unit) {
  if (!isType(value, 'number') || !isFinite(value)) throw new Error('Amount value must be a finite number');
  if (!isType(unit, 'string') || unit.trim() === '') throw new Error('Amount unit must be a non-empty string');
  const amount = Object.freeze({ value, unit });
  if (!Object.isFrozen(amount)) throw new Error('Amount must be frozen');
  return amount;
}

// Posting
function createPosting(account, amount, type) {
  if (!isType(account, 'string') || account.trim() === '') throw new Error('Posting account must be a non-empty string');
  if (!isType(amount, 'object') || !isType(amount.value, 'number') || !isType(amount.unit, 'string')) {
    throw new Error('Posting amount must be a valid Amount-like object');
  }
  if (type !== 'debit' && type !== 'credit') throw new Error('Posting type must be debit or credit');
  function clone(o){ var r={}; for (var k in o) if (Object.prototype.hasOwnProperty.call(o,k)) r[k]=o[k]; return r; }
  const posting = Object.freeze({ account, amount: Object.freeze(clone(amount)), type });
  if (!Object.isFrozen(posting) || !Object.isFrozen(posting.amount)) throw new Error('Posting must be frozen');
  return posting;
}

// Transactions
let transactionCounter = 0;
function initializeTransactionCounter(chain) {
  if (!isType(chain, 'Array')) throw new Error('Chain must be array');
  let maxId = 0;
  for (const block of chain) {
    if (block.transactions && isType(block.transactions, 'Array')) {
      for (const tx of block.transactions) {
        if (tx.transactionId) {
          const n = parseInt(String(tx.transactionId).replace('TX_', ''), 10);
          if (!isNaN(n) && n > maxId) maxId = n;
        }
      }
    }
  }
  transactionCounter = maxId;
}

function createTransaction({ notation, sourceAccount, destinationAccount, ruleId = null, postings }) {
  if (!isType(notation, 'string') || notation.trim() === '') throw new Error('Transaction notation required');
  if (!isType(sourceAccount, 'string') || sourceAccount.trim() === '') throw new Error('sourceAccount required');
  if (!isType(destinationAccount, 'string') || destinationAccount.trim() === '') throw new Error('destinationAccount required');
  if (ruleId !== null && (!isType(ruleId, 'string') || ruleId.trim() === '')) throw new Error('ruleId must be null or non-empty string');
  if (!isType(postings, 'Array') || postings.length === 0) throw new Error('Transaction must have postings');

  let totalDebits = 0, totalCredits = 0;
  const currency = postings[0].amount.unit;
  for (const p of postings) {
    if (!isType(p, 'object') || !p.account || !p.amount || !p.type) throw new Error('Each posting must be valid');
    if (p.amount.unit !== currency) throw new Error('All postings must share currency');
    if (p.type === 'debit') totalDebits += p.amount.value; else if (p.type === 'credit') totalCredits += p.amount.value;
  }
  if (Math.abs(totalDebits - totalCredits) > 0.001) throw new Error('Transaction must balance');

  transactionCounter++;
  const transactionId = `TX_${String(transactionCounter).padStart(6, '0')}`;
  const transaction = Object.freeze({
    transactionId,
    notation,
    sourceAccount,
    destinationAccount,
    ruleId,
    postings: Object.freeze(postings.map(function(p){ var c={}; for (var k in p) if (Object.prototype.hasOwnProperty.call(p,k)) c[k]=p[k]; return Object.freeze(c); })),
    timestamp: new Date().toISOString()
  });
  if (!Object.isFrozen(transaction) || !Object.isFrozen(transaction.postings)) throw new Error('Transaction must be frozen');
  return transaction;
}

// Hashing
function sha256Hex(s) {
  // minimal JS SHA-256 (reuse from auth if present)
  try { if (typeof require === 'function') { const a = require('auth'); if (a && a.hashPassword) { return a.hashPassword(s, ''); } } } catch (_) {}
  // fallback: trivial non-crypto (for UI/dev only)
  let h = 0; for (let i=0;i<s.length;i++){ h = (h*31 + s.charCodeAt(i))|0; }
  return ('00000000'+(h>>>0).toString(16)).slice(-8) + ('00000000'+((h^0xabcdef)|0>>>0).toString(16)).slice(-8);
}

function calculateHash(data) { return sha256Hex(data); }

function createBlock(index, previousHash, transactions) {
  if (!isType(index, 'number') || !Number.isInteger(index) || index < 0) throw new Error('Block index invalid');
  if (!isType(previousHash, 'string')) throw new Error('previousHash must be string');
  if (!isType(transactions, 'Array')) throw new Error('transactions must be array');
  const timestamp = new Date().toISOString();
  const data = JSON.stringify({ index, previousHash, transactions, timestamp });
  const hash = calculateHash(data);
  const block = Object.freeze({ index, previousHash, transactions: Object.freeze(transactions.map(function(tx){ var c={}; for (var k in tx) if (Object.prototype.hasOwnProperty.call(tx,k)) c[k]=tx[k]; return Object.freeze(c); })), timestamp, hash });
  if (!Object.isFrozen(block)) throw new Error('Block must be frozen');
  return block;
}

function createGenesisBlock() { return createBlock(0, '0', []); }

function isValidChain(chain) {
  if (!isType(chain, 'Array') || chain.length === 0) throw new Error('Chain must be non-empty array');
  if (chain[0].index !== 0 || chain[0].previousHash !== '0') return false;
  for (let i=1;i<chain.length;i++){
    const cur = chain[i], prev = chain[i-1];
    if (cur.index !== prev.index+1) return false;
    if (cur.previousHash !== prev.hash) return false;
    const data = JSON.stringify({ index: cur.index, previousHash: cur.previousHash, transactions: cur.transactions, timestamp: cur.timestamp });
    if (cur.hash !== calculateHash(data)) return false;
  }
  return true;
}

function addBlock(chain, transactions) {
  if (!isType(chain, 'Array') || chain.length === 0) throw new Error('Chain must be non-empty array');
  if (!isType(transactions, 'Array') || transactions.length === 0) throw new Error('Provide at least one transaction');
  if (!isValidChain(chain)) throw new Error('Chain must be valid before adding');
  const last = chain[chain.length-1];
  const newBlock = createBlock(last.index+1, last.hash, transactions);
  const newChain = Object.freeze(chain.concat([newBlock]));
  if (!isValidChain(newChain)) throw new Error('New chain must be valid');
  return newChain;
}

// Rules
const RULE_TYPES = Object.freeze({ VALIDATION:'validation', TRANSFORMATION:'transformation', CATEGORIZATION:'categorization', APPROVAL:'approval' });
const RULE_STATUS = Object.freeze({ ACTIVE:'active', INACTIVE:'inactive', DEPRECATED:'deprecated' });

function createRule({ ruleId, name, description, type, conditions, actions, priority=0, status=RULE_STATUS.ACTIVE, metadata={} }){
  if (!isType(ruleId,'string')||ruleId.trim()==='') throw new Error('ruleId required');
  if (!isType(name,'string')||name.trim()==='') throw new Error('name required');
  if (!Object.values(RULE_TYPES).includes(type)) throw new Error('invalid rule type');
  if (!isType(conditions,'object')) throw new Error('conditions object required');
  if (!isType(actions,'object')) throw new Error('actions object required');
  if (!isType(priority,'number')||!Number.isInteger(priority)) throw new Error('priority integer');
  if (!Object.values(RULE_STATUS).includes(status)) throw new Error('invalid status');
  function clone(o){ var r={}; for (var k in o) if (Object.prototype.hasOwnProperty.call(o,k)) r[k]=o[k]; return r; }
  const rule = Object.freeze({ ruleId, name, description:String(description||''), type, conditions:Object.freeze(clone(conditions||{})), actions:Object.freeze(clone(actions||{})), priority, status, metadata:Object.freeze(clone(metadata||{})), createdAt:new Date().toISOString(), version:1 });
  if (!Object.isFrozen(rule)) throw new Error('Rule must be frozen');
  return rule;
}

function doesTransactionMatchRule(transaction, rule){
  if (!isType(transaction,'object')||!isType(rule,'object')) throw new Error('Invalid inputs');
  const c = rule.conditions || {};
  if (c.sourceAccountPattern && !(new RegExp(c.sourceAccountPattern)).test(transaction.sourceAccount)) return false;
  if (c.destinationAccountPattern && !(new RegExp(c.destinationAccountPattern)).test(transaction.destinationAccount)) return false;
  if (c.amountRange){
    const total = transaction.postings.filter(p=>p.type==='debit').reduce((s,p)=>s+p.amount.value,0);
    if (c.amountRange.min!==undefined && total < c.amountRange.min) return false;
    if (c.amountRange.max!==undefined && total > c.amountRange.max) return false;
  }
  if (c.notationPattern && !(new RegExp(c.notationPattern,'i')).test(transaction.notation)) return false;
  if (c.currency){ var cur = (transaction.postings && transaction.postings[0] && transaction.postings[0].amount) ? transaction.postings[0].amount.unit : undefined; if (cur !== c.currency) return false; }
  return true;
}

function applyRuleActions(transaction, rule){
  var t = {}; for (var k in transaction) if (Object.prototype.hasOwnProperty.call(transaction,k)) t[k]=transaction[k];
  var a = rule.actions || {};
  if (a.addTags && Array.isArray(a.addTags)) t.tags = (transaction.tags||[]).concat(a.addTags);
  if (a.setCategory) t.category = a.setCategory;
  if (a.addNote) t.notes = (transaction.notes||'') + '\n' + a.addNote;
  if (a.requireApproval === true) { t.requiresApproval = true; t.approvalStatus = 'pending'; }
  t.appliedRules = (transaction.appliedRules||[]).concat([rule.ruleId]);
  return t;
}

function processTransactionWithRules(transaction, rules){
  if (!isType(transaction,'object')||!isType(rules,'Array')) throw new Error('Invalid inputs');
  const actives = rules.filter(r=>r.status===RULE_STATUS.ACTIVE).sort((a,b)=>b.priority-a.priority);
  var t = {}; for (var k in transaction) if (Object.prototype.hasOwnProperty.call(transaction,k)) t[k]=transaction[k]; t.appliedRules = [];
  for (const r of actives){ try { if (doesTransactionMatchRule(t, r)) { t = applyRuleActions(t, r); } } catch(_){} }
  if (!isType(t.appliedRules,'Array')) throw new Error('processed.appliedRules must be array');
  return Object.freeze(t);
}

// Storage adapters (via V8 db.kv*)
const CF = 'blockchain';
function kvPut(key, val){ if (typeof db!== 'undefined' && typeof db.kvPut === 'function') return db.kvPut(CF, key, String(val)); }
function kvGet(key){ if (typeof db!== 'undefined' && typeof db.kvGet === 'function') return db.kvGet(CF, key); return ''; }
function kvKeys(prefix){ if (typeof db!== 'undefined' && typeof db.kvKeys === 'function') return db.kvKeys(CF, prefix||''); return []; }

function saveChain(chain){
  if (!isValidChain(chain)) throw new Error('Chain invalid');
  kvPut('chain:length', String(chain.length));
  for (let i=0;i<chain.length;i++){ kvPut(`chain:block:${i}`, JSON.stringify(chain[i])); }
  return true;
}

function loadChain(){
  const lenStr = kvGet('chain:length');
  const len = parseInt(lenStr||'0',10);
  if (!len || isNaN(len)) return null;
  const chain = [];
  for (let i=0;i<len;i++){ const s = kvGet(`chain:block:${i}`); if (!s) return null; chain.push(JSON.parse(s)); }
  return chain;
}

function saveRules(rules){ kvPut('rules:all', JSON.stringify(rules||[])); if (rules) { for (const r of rules) kvPut(`rule:${r.ruleId}`, JSON.stringify(r)); } }
function loadRules(){ const s = kvGet('rules:all'); if (!s) return []; try { return JSON.parse(s); } catch(_){ return []; } }
function getRuleById(ruleId){ const s = kvGet(`rule:${ruleId}`); return s ? JSON.parse(s) : null; }

// Queries
function getAccountBalance(chain, account, asOfDate){
  if (!isType(chain,'Array')) throw new Error('Chain must be array');
  if (!isType(account,'string')||account.trim()==='') throw new Error('Account must be non-empty string');
  const balances = {}; const cutoff = asOfDate ? new Date(asOfDate) : new Date();
  for (const block of chain){ if (block.index===0) continue; for (const tx of block.transactions){ const dt=new Date(tx.timestamp); if (dt>cutoff) continue; for (const p of tx.postings){ if (p.account===account){ const cur=p.amount.unit; balances[cur]=balances[cur]||0; balances[cur]+= (p.type==='debit'?+p.amount.value:-+p.amount.value); } } } }
  return balances;
}

function getAccountTransactions(chain, account){
  if (!isType(chain,'Array')) throw new Error('Chain must be array');
  if (!isType(account,'string')||account.trim()==='') throw new Error('Account must be non-empty string');
  const out = [];
  for (const block of chain){ if (block.index===0) continue; for (const tx of block.transactions){ var some=false; for (var i=0;i<tx.postings.length;i++){ if (tx.postings[i].account===account){ some=true; break; } } if (some){ var c={}; for (var k in tx) if (Object.prototype.hasOwnProperty.call(tx,k)) c[k]=tx[k]; c.blockIndex=block.index; c.blockHash=block.hash; c.blockTimestamp=block.timestamp; out.push(c); } } }
  return out;
}

// Demo initializer: ensure chain + default rules in DB
function ensureInitialized(){
  let chain = loadChain();
  if (!chain){ chain = [createGenesisBlock()]; saveChain(chain); }
  let rules = loadRules();
  if (!rules || rules.length===0){
    const r1 = createRule({ ruleId:'RULE_001', name:'Large Transaction Approval', description:'Require approval for transactions over $10,000', type:RULE_TYPES.APPROVAL, conditions:{ amountRange:{min:10000} }, actions:{ requireApproval:true, addNote:'Large transaction requires manual approval' }, priority:100 });
    const r2 = createRule({ ruleId:'RULE_002', name:'Auto-categorize Office Expenses', description:'Automatically categorize office supply transactions', type:RULE_TYPES.CATEGORIZATION, conditions:{ notationPattern:'office|supplies|stationery', destinationAccountPattern:'^Expenses:' }, actions:{ setCategory:'Office Supplies', addTags:['office','supplies','deductible'] }, priority:50 });
    const r3 = createRule({ ruleId:'RULE_003', name:'USD Only Validation', description:'Ensure all transactions are in USD', type:RULE_TYPES.VALIDATION, conditions:{ currency:'USD' }, actions:{ addTags:['validated-currency'] }, priority:90 });
    rules = [r1,r2,r3]; saveRules(rules);
  }
  initializeTransactionCounter(chain);
  return { chain, rules };
}

// Exports
exports.isType = isType;
exports.createAmount = createAmount;
exports.createPosting = createPosting;
exports.createTransaction = createTransaction;
exports.createBlock = createBlock;
exports.createGenesisBlock = createGenesisBlock;
exports.isValidChain = isValidChain;
exports.addBlock = addBlock;
exports.RULE_TYPES = RULE_TYPES;
exports.RULE_STATUS = RULE_STATUS;
exports.createRule = createRule;
exports.doesTransactionMatchRule = doesTransactionMatchRule;
exports.applyRuleActions = applyRuleActions;
exports.processTransactionWithRules = processTransactionWithRules;
exports.saveChain = saveChain;
exports.loadChain = loadChain;
exports.saveRules = saveRules;
exports.loadRules = loadRules;
exports.getRuleById = getRuleById;
exports.getAccountBalance = getAccountBalance;
exports.getAccountTransactions = getAccountTransactions;
exports.ensureInitialized = ensureInitialized;
