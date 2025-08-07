function checkParams(params, required) {
  required.forEach(key => {
    if (!(key in params)) throw new Error("Missing " + key);
  });
}

// sanitize.js

function checkParams(params, required) {
  required.forEach(key => {
    if (!(key in params)) throw new Error("Missing " + key);
  });
}

// Recursively serialize *any* JS value into JSON text.
// - Primitives (string/number/boolean/null) get handled.
// - undefined & symbols are skipped (properties dropped).
// - Arrays preserve holes as null.
// - Objects (and Functions) have only their own enumerable props.
// - Circular refs become `null`.
function safeStringify(root) {
	return JSON.stringify(root);
}

exports.checkParams   = checkParams;
exports.safeStringify = safeStringify;

