const SECRET = 'qsecret';
exports.login = function(username, password) {
  return CppSignJwt(JSON.stringify({ user: username }), SECRET);
};
exports.verify = function(token) {
  return CppVerifyJwt(token, SECRET);
};
