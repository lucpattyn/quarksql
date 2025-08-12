#pragma once

#include <string>

// Starts a background thread to watch a directory (default: "requirements/")
// for new or modified markdown files (e.g., agent.md). When a file appears,
// it parses the spec and generates:
// - scripts/<slug>.js (business logic extending global `api`)
// - public/<slug>/{index.html, app.js, style.css}
// It also appends a `require('<slug>')` line to scripts/business.js if missing
// and hot-loads the module into V8 so the API is available without restart.
//
// Call this once during startup; it is safe to call with the default dir.
void StartRequirementsWatcher(const std::string& requirementsDir = "requirements");

