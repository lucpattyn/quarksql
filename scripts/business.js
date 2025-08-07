// scripts/business.js

const sanitize = require('sanitize');
const auth     = require('auth');

var api = api || {};

// — LOGIN —
api.login = {
  params: ['username','password'],
  handler: function(params) {
    sanitize.checkParams(params, this.params);
    const token = auth.login(params.username, params.password);
    return { token };
  }
};

// — VERIFY —
api.verify = {
  params: ['token'],
  handler: function(params) {
    sanitize.checkParams(params, this.params);
    const payloadJson = auth.verify(params.token);
    return { data: JSON.parse(payloadJson) };
  }
};

// — LIST —
api.list = {
  params: [],
  handler: function() {
    return Object.keys(api).map(fn => ({
      name: fn,
      params: api[fn].params
    }));
  }
};

// — RUN A SELECT QUERY —
api.query = {
  params: ['sql'],
  handler: function(params) {
    sanitize.checkParams(params, this.params);
    // db.query is your C++ binding
    return db.query(params.sql);
  }
};

// — EXECUTE A WRITE (INSERT/UPDATE/DELETE) —
api.execute = {
  params: ['sql'],
  handler: function(params) {
    sanitize.checkParams(params, this.params);
    // db.execute returns { success: true } or { success: false, error: ... }
    return db.execute(params.sql);
  }
};

