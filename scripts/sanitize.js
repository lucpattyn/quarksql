
// scripts/sanitize.js

function isString(x){ return typeof x === 'string'; }
function isNumber(x){ return typeof x === 'number' && isFinite(x); }
function isArray(x){ return Array.isArray(x); }
function isObject(x){ return x && typeof x === 'object' && !Array.isArray(x); }

function checkParams(obj, required) {
  if (!isObject(obj)) throw new Error('Invalid params');
  for (var i=0;i<required.length;i++) {
    var k = required[i];
    if (!(k in obj)) throw new Error('Missing param: ' + k);
  }
}

function requireAuth(token){
  if (!token || !isString(token) || token.length < 10) {
    throw new Error('Auth token missing or invalid');
  }
}

function nonEmptyString(s, name){
  if (!isString(s) || !s.trim()) throw new Error('Invalid ' + (name||'string'));
  return s.trim();
}

function positiveNumber(n, name){
  if (!isNumber(n) || n < 0) throw new Error('Invalid ' + (name||'number'));
  return n;
}

function isoDate(s, name){
  nonEmptyString(s, name||'date');
  if (!/^\d{4}-\d{2}-\d{2}/.test(s)) throw new Error('Date must be ISO format YYYY-MM-DD');
  return s;
}

function ensureBalancedLines(lines){
  if (!isArray(lines) || !lines.length) throw new Error('Lines must be a non-empty array');
  var totalDebit = 0, totalCredit = 0;
  for (var i=0;i<lines.length;i++){
    var l = lines[i];
    if (!isObject(l)) throw new Error('Line '+i+' invalid');
    var d = +(+l.debit || 0).toFixed(2);
    var c = +(+l.credit || 0).toFixed(2);
    if (d<0 || c<0) throw new Error('Negative amounts not allowed');
    if (d>0 && c>0) throw new Error('Line '+i+' cannot have both debit and credit');
    if (!l.account_code || typeof l.account_code !== 'string') throw new Error('Missing account_code on line '+i);
    totalDebit += d;
    totalCredit += c;
  }
  if (+totalDebit.toFixed(2) !== +totalCredit.toFixed(2)) {
    throw new Error('Unbalanced entry: debits '+totalDebit.toFixed(2)+' != credits '+totalCredit.toFixed(2));
  }
}

function safeStringify(value){
  try { return JSON.stringify(value); } catch(e){ return '[]'; }
}

var api = {
  checkParams: checkParams,
  requireAuth: requireAuth,
  nonEmptyString: nonEmptyString,
  positiveNumber: positiveNumber,
  isoDate: isoDate,
  ensureBalancedLines: ensureBalancedLines,
  safeStringify: safeStringify
};

if (typeof exports !== 'undefined') {
  for (var k in api) exports[k] = api[k];
} else {
  this.sanitize = api;
}

