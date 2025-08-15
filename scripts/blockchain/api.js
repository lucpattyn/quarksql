// scripts/blockchain/api.js
// Wires blockchain ledger functions into global api.* handlers

var sanitize = (typeof sanitize !== 'undefined') ? sanitize : require('sanitize');
var auth     = (typeof auth     !== 'undefined') ? auth     : require('auth');
var ledger   = require('blockchain/ledger');

var api = (typeof api !== 'undefined') ? api : (this.api = {});

function requireUser(token){
  sanitize.requireAuth(token);
  var payloadJson = auth.verify(token);
  return JSON.parse(payloadJson).sub;
}

api.bc_init = {
  params: ['token'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    var st = ledger.ensureInitialized();
    return { ok: true, blocks: st.chain.length, rules: st.rules.length };
  }
};

api.bc_addTransaction = {
  params: ['token','notation','sourceAccount','destinationAccount','amount','currency'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    var st = ledger.ensureInitialized();
    var amount = ledger.createAmount(+p.amount, p.currency || 'USD');
    var postings = [
      ledger.createPosting(p.sourceAccount, amount, 'credit'),
      ledger.createPosting(p.destinationAccount, amount, 'debit')
    ];
    var tx = ledger.createTransaction({ notation:p.notation, sourceAccount:p.sourceAccount, destinationAccount:p.destinationAccount, postings });
    var processed = ledger.processTransactionWithRules(tx, st.rules);
    var chain = ledger.addBlock(st.chain, [processed]);
    ledger.saveChain(chain);
    return { ok: true, blockIndex: chain[chain.length-1].index, transactionId: processed.transactionId, appliedRules: processed.appliedRules||[] };
  }
};

api.bc_getBalance = {
  params: ['token','account','asOf'],
  handler: function(p){
    sanitize.checkParams(p, ['token','account']);
    requireUser(p.token);
    var st = ledger.ensureInitialized();
    var bal = ledger.getAccountBalance(st.chain, p.account, p.asOf||null);
    return { ok: true, account: p.account, balances: bal };
  }
};

api.bc_getTransactions = {
  params: ['token','account'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    var st = ledger.ensureInitialized();
    var txs = ledger.getAccountTransactions(st.chain, p.account);
    return { ok: true, transactions: txs };
  }
};

api.bc_listRules = {
  params: ['token'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    var rules = ledger.loadRules();
    return { ok: true, rules: rules };
  }
};

api.bc_addRule = {
  params: ['token','rule'],
  handler: function(p){
    sanitize.checkParams(p, this.params);
    requireUser(p.token);
    var st = ledger.ensureInitialized();
    var rule = ledger.createRule(p.rule);
    var rules = st.rules.concat([rule]).sort(function(a,b){ return b.priority - a.priority; });
    ledger.saveRules(rules);
    return { ok: true, ruleId: rule.ruleId };
  }
};

