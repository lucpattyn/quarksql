// auth.js (CommonJS-style for Quarksql, with secret for JWT)

// --- Config ---
const JWT_SECRET = "QuarksSecret"; // TODO: put in secure config

// --- Minimal SHA-256 ---
function sha256(ascii) {
  function rightRotate(value, amount) { return (value>>>amount) | (value<<(32-amount)); }
  var mathPow = Math.pow; var maxWord = mathPow(2, 32);
  var lengthProperty = 'length';
  var i, j; var result = '';

  var words = [];
  var asciiBitLength = ascii[lengthProperty]*8;

  var hash = sha256.h = sha256.h || [];
  var k = sha256.k = sha256.k || [];
  var primeCounter = k[lengthProperty];
  var isComposite = {};
  for (var candidate = 2; primeCounter < 64; candidate++) {
    if (!isComposite[candidate]) {
      for (i = 0; i < 313; i += candidate) isComposite[i] = candidate;
      hash[primeCounter] = (mathPow(candidate, .5)*maxWord)|0;
      k[primeCounter++] = (mathPow(candidate, 1/3)*maxWord)|0;
    }
  }

  ascii += '\x80';
  while (ascii[lengthProperty]%64 - 56) ascii += '\x00';
  for (i = 0; i < ascii[lengthProperty]; i++) {
    j = ascii.charCodeAt(i);
    words[i>>2] |= j << ((3 - i)%4)*8;
  }
  words[words[lengthProperty]] = ((asciiBitLength/maxWord)|0);
  words[words[lengthProperty]] = (asciiBitLength);

  for (j = 0; j < words[lengthProperty];) {
    var w = words.slice(j, j += 16);
    var oldHash = hash;
    hash = hash.slice(0, 8);

    for (i = 0; i < 64; i++) {
      var w15 = w[i - 15], w2 = w[i - 2];
      var a = hash[0], e = hash[4];
      var temp1 = hash[7]
        + (rightRotate(e, 6) ^ rightRotate(e, 11) ^ rightRotate(e, 25))
        + ((e&hash[5])^((~e)&hash[6]))
        + k[i]
        + (w[i] = (i < 16) ? w[i] : (
            w[i - 16]
            + (rightRotate(w15, 7) ^ rightRotate(w15, 18) ^ (w15>>>3))
            + w[i - 7]
            + (rightRotate(w2, 17) ^ rightRotate(w2, 19) ^ (w2>>>10))
          )|0
          );
      var temp2 = (rightRotate(a, 2) ^ rightRotate(a, 13) ^ rightRotate(a, 22))
        + ((a&hash[1])^(a&hash[2])^(hash[1]&hash[2]));

      hash = [(temp1 + temp2)|0].concat(hash);
      hash[4] = (hash[4] + temp1)|0;
    }

    for (i = 0; i < 8; i++) {
      hash[i] = (hash[i] + oldHash[i])|0;
    }
  }

  for (i = 0; i < 8; i++) {
    for (j = 3; j + 1; j--) {
      var b = (hash[i] >> (j * 8)) & 255;
      result += ((b < 16) ? 0 : '') + b.toString(16);
    }
  }
  return result;
}

// --- Utilities ---
function randomId(len){
  len = len || 16;
  var s = '';
  var chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789';
  for (var i=0;i<len;i++) s += chars[Math.floor(Math.random()*chars.length)];
  return s;
}

function hashPassword(password, salt){
  return sha256(salt + '|' + password);
}

// --- C++ Bindings Wrappers ---
function CppSignJwtSafe(payloadJson){
  if (typeof CppSignJwt !== 'function') throw new Error('CppSignJwt not available');
  return CppSignJwt(payloadJson, JWT_SECRET);
}
function CppVerifyJwtSafe(token){
  if (typeof CppVerifyJwt !== 'function') throw new Error('CppVerifyJwt not available');
  return CppVerifyJwt(token, JWT_SECRET);
}

// --- Auth flows ---
function signup(username, password){
  username = String(username || '').trim().toLowerCase();
  var existing = db.query("SELECT * FROM users WHERE username = '" + username + "';");
  if (existing && existing.length) throw new Error('User exists');

  var salt = randomId(16);
  var hash = hashPassword(password, salt);
  var now = new Date().toISOString();
  var user = { username: username, password_hash: hash, salt: salt, created_at: now, role: 'user' };
  db.execute("INSERT INTO users VALUES " + JSON.stringify(user) + ";");
  return { success: true };
}

function login(username, password){
  username = String(username || '').trim().toLowerCase();
  var rows = db.query("SELECT * FROM users WHERE username = '" + username + "';");
  if (!rows || !rows.length) throw new Error('Invalid credentials');
  var u = rows[0];
  var hash = hashPassword(password, u.salt);
  if (hash !== u.password_hash) throw new Error('Invalid credentials');

  var token = CppSignJwtSafe(JSON.stringify({ sub: username, role: u.role, iat: Math.floor(Date.now()/1000) }));
  return token;
}

function verify(token){
  return CppVerifyJwtSafe(token); // returns payload JSON string
}

// --- Exports ---
exports.signup = signup;
exports.login = login;
exports.verify = verify;
exports.hashPassword = hashPassword;
exports.randomId = randomId;

