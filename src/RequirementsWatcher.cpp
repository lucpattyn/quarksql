#include "RequirementsWatcher.h"

#include <sys/inotify.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <iomanip>
#include <algorithm>
#include <cctype>

#include <v8.h>
#include <json.hpp>

// Externs from main.cpp to hot-load modules into V8
extern v8::Isolate* g_isolate;
extern v8::Persistent<v8::Context> persistent_ctx;

namespace fs = std::filesystem;

static std::mutex g_genMutex; // serialize generations

// --- tiny helpers -----------------------------------------------------------
static std::string sluggify(const std::string& name) {
    std::string s;
    s.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) s.push_back(std::tolower(c));
        else if (c==' ' || c=='-' || c=='_' ) { if (s.empty() || s.back()!='-') s.push_back('-'); }
    }
    if (!s.empty() && s.back()=='-') s.pop_back();
    if (s.empty()) s = "app";
    return s;
}

static bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p);
    if (!f.good()) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool writeFile(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    if (!f.good()) return false;
    f << data;
    return true;
}

static bool appendIfMissing(const fs::path& p, const std::string& needle, const std::string& toAppend) {
    std::string body;
    if (!readFile(p, body)) return false;
    if (body.find(needle) != std::string::npos) return true; // already present
    body += toAppend;
    return writeFile(p, body);
}

// --- parse agent.md ---------------------------------------------------------
struct EndpointSpec {
    std::string name;
    std::vector<std::string> params; // e.g., ["token","id"]
    std::string sql;  // for SELECT
    std::string exec; // for INSERT/UPDATE/DELETE/BATCH
};

struct AppSpec {
    std::string name;
    std::string slug;
    std::vector<EndpointSpec> endpoints;
};

// Tries to extract a JSON block fenced with ```json requirements\n{...}\n```
// If found, parses endpoints. Otherwise, creates a single hello endpoint.
static AppSpec parseRequirements(const std::string& md) {
    AppSpec app;
    // 1) Name/Slug by scanning lines (portable: no multiline regex needed)
    {
        std::istringstream iss(md);
        std::string line;
        auto ltrim = [](std::string s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch){return !std::isspace(ch);})); return s; };
        auto rtrim = [](std::string s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch){return !std::isspace(ch);}).base(), s.end()); return s; };
        while (std::getline(iss, line)) {
            std::string raw = rtrim(line);
            if (app.name.empty()) {
                if (raw.rfind("#", 0) == 0) {
                    // strip leading #'s and spaces
                    size_t i = 0; while (i < raw.size() && raw[i] == '#') ++i; while (i < raw.size() && std::isspace((unsigned char)raw[i])) ++i;
                    app.name = raw.substr(i);
                } else {
                    std::string low = raw; for (char& c: low) c = (char)std::tolower((unsigned char)c);
                    if (low.rfind("title:", 0) == 0) {
                        app.name = ltrim(raw.substr(6));
                    }
                }
            }
            if (app.slug.empty()) {
                std::string low = raw; for (char& c: low) c = (char)std::tolower((unsigned char)c);
                if (low.rfind("slug:", 0) == 0) {
                    app.slug = ltrim(raw.substr(5));
                }
            }
        }
    }
    if (app.name.empty()) app.name = "Generated App";
    if (app.slug.empty()) app.slug = sluggify(app.name);

    // 2) Fenced JSON block: find "```json requirements" then the next "```"
    try {
        std::string lower = md; for (char& c: lower) c = (char)std::tolower((unsigned char)c);
        const std::string marker = "```json requirements";
        size_t mpos = lower.find(marker);
        if (mpos != std::string::npos) {
            size_t lineEnd = md.find('\n', mpos);
            size_t start = (lineEnd == std::string::npos) ? (mpos + marker.size()) : (lineEnd + 1);
            size_t end = md.find("```", start);
            if (end != std::string::npos && end > start) {
                std::string jtxt = md.substr(start, end - start);
                nlohmann::json j = nlohmann::json::parse(jtxt);
                if (j.contains("name")) app.name = j["name"].get<std::string>();
                if (j.contains("slug")) app.slug = j["slug"].get<std::string>();
                if (j.contains("api") && j["api"].is_array()) {
                    for (auto& ep : j["api"]) {
                        EndpointSpec e;
                        e.name = ep.value("name", std::string());
                        if (ep.contains("params")) for (auto& p : ep["params"]) e.params.push_back(p.get<std::string>());
                        e.sql  = ep.value("sql", std::string());
                        e.exec = ep.value("exec", std::string());
                        if (!e.name.empty()) app.endpoints.push_back(std::move(e));
                    }
                }
            }
        }
    } catch (...) {
        // Ignore parse errors; fall back to hello endpoint
    }

    if (app.endpoints.empty()) {
        EndpointSpec e;
        e.name = app.slug + std::string("_hello");
        e.params = {"token"};
        e.sql = "SELECT * FROM projects;";
        app.endpoints.push_back(std::move(e));
    }
    return app;
}

// --- codegen ---------------------------------------------------------------
static std::string genBusinessModule(const AppSpec& app) {
    std::ostringstream o;
    o << "\n// Auto-generated module for app: " << app.name << " (" << app.slug << ")\n";
    o << "var _api = (typeof globalThis !== 'undefined' && globalThis.api) ? globalThis.api : (this.api = this.api || {});\n\n";

    auto jsQuote = [](const std::string& s){
        std::string out; out.reserve(s.size()+8); out.push_back('"');
        for (unsigned char c : s) {
            switch (c) {
                case '\\': out += "\\\\"; break;
                case '"':  out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': /* skip */ break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        // control char: skip
                    } else {
                        out.push_back((char)c);
                    }
            }
        }
        out.push_back('"');
        return out;
    };

    for (const auto& ep : app.endpoints) {
        // Decide operation type
        bool isSelect = !ep.sql.empty();
        std::string sqlOrExec = isSelect ? ep.sql : ep.exec;
        // basic param list
        std::ostringstream params;
        params << "[";
        for (size_t i = 0; i < ep.params.size(); ++i) {
            if (i) params << ",";
            params << "\"" << ep.params[i] << "\"";
        }
        params << "]";

        o << "_api." << ep.name << " = {\n";
        o << "  params: " << params.str() << ",\n";
        o << "  handler: function(p){\n";
        o << "    sanitize.checkParams(p, this.params);\n";
        o << "    if (p.token) sanitize.requireAuth(p.token);\n";
        // simple ${param} substitution in the provided SQL/exec string
        o << "    var tpl = " << jsQuote(sqlOrExec) << ";\n";
        o << "    var sql = tpl.replace(/\\$\\{([a-zA-Z0-9_]+)\\}/g, function(_, k){\n";
        o << "      var v = (p[k]===undefined? '': p[k]);\n";
		o << "      if (typeof v === 'number') return String(v);\n";
		o << "      if (typeof v === 'string' && v.trim()!=='' && !isNaN(v)) return String(Number(v));\n";
		o << "      return \"'\" + String(v).replace(/'/g, \"''\") + \"'\";\n";

        o << "    });\n";
        if (isSelect) {
            o << "    return db.query(sql);\n";
        } else {
            o << "    db.execute(sql); return { ok: true };\n";
        }
        o << "  }\n";
        o << "};\n\n";
    }

    o << "// End of autogenerated module\n";
    return o.str();
}

static std::string genIndexHtml(const AppSpec& app) {
    std::ostringstream o;
    o << "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\"/>\n";
    o << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n";
    o << "<title>" << app.name << "</title>\n<link rel=\"stylesheet\" href=\"style.css\"/>\n</head>\n<body>\n";
    o << "<header><h1>" << app.name << "</h1></header>\n<main>\n";
    o << "<section id=\"auth\"><label>Token</label><input id=\"token\" placeholder=\"paste JWT (optional)\"/></section>\n";
    o << "<section id=\"actions\"><h2>Endpoints</h2><div id=\"endpoints\"></div></section>\n";
    o << "<section id=\"io\">\n<h2>Params</h2>\n<textarea id=\"params\" rows=\"8\" placeholder=\"{\n  &quot;token&quot;: &quot;&quot;\n}\"></textarea>\n";
    o << "<button id=\"runBtn\">Run</button>\n<h2>Result</h2><pre id=\"out\"></pre>\n</section>\n";
    o << "</main>\n<script>window.__APP_META__ = ";
    nlohmann::json meta;
    meta["name"] = app.name;
    meta["slug"] = app.slug;
    for (const auto& ep : app.endpoints) {
        nlohmann::json jep;
        jep["name"] = ep.name;
        jep["params"] = ep.params;
        meta["endpoints"].push_back(jep);
    }
    o << meta.dump() << ";</script>\n";
    o << "<script src=\"app.js\"></script>\n</body>\n</html>\n";
    return o.str();
}

static std::string genAppJs(const AppSpec& app) {
    std::ostringstream o;
    o << "(function(){\n";
    o << "const meta = window.__APP_META__ || {endpoints:[]};\n";
    o << "const eps = meta.endpoints;\n";
    o << "const list = document.getElementById('endpoints');\n";
    o << "let selected = eps.length ? eps[0].name : '';\n";
    o << "function render(){ list.innerHTML=''; eps.forEach(ep=>{ const b=document.createElement('button'); b.textContent=ep.name; b.onclick=()=>{selected=ep.name;}; list.appendChild(b); }); }\n";
    o << "render();\n";
    o << "document.getElementById('runBtn').onclick = async function(){\n";
    o << "  const token = document.getElementById('token').value.trim();\n";
    o << "  let params = {}; try { params = JSON.parse(document.getElementById('params').value||'{}'); } catch(e){ alert('Invalid JSON in params'); return; }\n";
    o << "  if (token) params.token = token;\n";
    o << "  const res = await fetch('/api/'+selected, { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(params)});\n";
    o << "  const txt = await res.text();\n";
    o << "  document.getElementById('out').textContent = txt;\n";
    o << "};\n";
    o << "})();\n";
    return o.str();
}

static std::string genStyleCss() {
    return "body{font-family:system-ui,Arial,sans-serif;margin:0;padding:0;background:#fafafa;color:#222}" \
           "header{background:#222;color:#fff;padding:12px 16px}" \
           "main{padding:16px;display:grid;gap:16px}" \
           "section{background:#fff;border:1px solid #ddd;border-radius:8px;padding:12px}" \
           "button{margin:4px;padding:6px 10px;border-radius:6px;border:1px solid #bbb;background:#f1f1f1;cursor:pointer}";
}

static void hotLoadModuleIntoV8(const std::string& slug) {
    if (!g_isolate) return;
    v8::Locker locker(g_isolate);
    v8::Isolate::Scope iscope(g_isolate);
    v8::HandleScope hs(g_isolate);
    auto ctx = persistent_ctx.Get(g_isolate);
    v8::Context::Scope cs(ctx);

    v8::Local<v8::Value> require_val;
    if (!ctx->Global()->Get(ctx, v8::String::NewFromUtf8(g_isolate, "require", v8::NewStringType::kNormal).ToLocalChecked()).ToLocal(&require_val)
        || !require_val->IsFunction()) {
        std::cerr << "[Watcher] require() not available in V8 context\n";
        return;
    }
    auto requireFn = require_val.As<v8::Function>();
    v8::Local<v8::Value> argv[] = { v8::String::NewFromUtf8(g_isolate, slug.c_str(), v8::NewStringType::kNormal).ToLocalChecked() };
    v8::TryCatch tc(g_isolate);
    (void)requireFn->Call(ctx, ctx->Global(), 1, argv);
    if (tc.HasCaught()) {
        v8::String::Utf8Value err(g_isolate, tc.Exception());
        std::cerr << "[Watcher] Error requiring module '" << slug << "': " << (*err?*err:"unknown") << "\n";
    } else {
        std::cout << "[Watcher] Loaded module '" << slug << "' into V8" << std::endl;
    }
}

static void generateFromMd(const fs::path& mdPath) {
    std::lock_guard<std::mutex> lk(g_genMutex);
    std::string md;
    if (!readFile(mdPath, md)) {
        std::cerr << "[Watcher] Cannot read " << mdPath << "\n";
        return;
    }
    AppSpec app = parseRequirements(md);

    // 1) scripts/<slug>.js
    fs::path modulePath = fs::path("scripts") / (app.slug + ".js");
    if (!writeFile(modulePath, genBusinessModule(app))) {
        std::cerr << "[Watcher] Failed to write " << modulePath << "\n";
        return;
    }

    // 2) public/<slug>/
    fs::path appDir = fs::path("public") / app.slug;
    if (!writeFile(appDir / "index.html", genIndexHtml(app)))
        std::cerr << "[Watcher] Failed to write index.html\n";
    if (!writeFile(appDir / "app.js", genAppJs(app)))
        std::cerr << "[Watcher] Failed to write app.js\n";
    if (!writeFile(appDir / "style.css", genStyleCss()))
        std::cerr << "[Watcher] Failed to write style.css\n";

    // 3) Append require('<slug>') to scripts/business.js if missing
    fs::path bizPath = fs::path("scripts") / "business.js";
    const std::string needle = std::string("require(\"") + app.slug + "\")";
    const std::string append = std::string("\n// auto-generated include for ") + app.slug + "\nrequire(\"" + app.slug + "\");\n";
    (void)appendIfMissing(bizPath, needle, append);

    // 4) Hot-load into V8 so new API endpoints become available immediately
    //hotLoadModuleIntoV8(app.slug);

    std::cout << "[Watcher] Generated app '" << app.name << "' at /public/" << app.slug << " and scripts/" << app.slug << ".js\n";
}

void StartRequirementsWatcher(const std::string& requirementsDir) {
    // Ensure directory exists
    try { fs::create_directories(requirementsDir); } catch (...) {}

    // If no .md present, scaffold a default template to guide the user
    try {
        bool hasMd = false;
        for (auto& e : fs::directory_iterator(requirementsDir)) {
            if (e.is_regular_file()) {
                auto p = e.path();
                if (p.extension() == ".md") { hasMd = true; break; }
            }
        }
        if (!hasMd) {
            const char* tpl =
                "# Title: Sample App\n\n"
                "Slug: sample\n\n"
                "Describe your app and endpoints. Optionally include a fenced JSON block with your API spec.\n\n"
                "```json requirements\n"
                "{\n"
                "  \"name\": \"Sample App\",\n"
                "  \"slug\": \"sample\",\n"
                "  \"api\": [\n"
                "    { \"name\": \"sample_list\", \"params\": [\"token\"], \"sql\": \"SELECT * FROM projects;\" }\n"
                "  ]\n"
                "}\n"
                "```\n";
            writeFile(fs::path(requirementsDir) / "agent.md", tpl);
        }
    } catch (...) {}

    std::thread([requirementsDir]{
        int fd = inotify_init1(IN_NONBLOCK);
        if (fd < 0) {
            std::cerr << "[Watcher] inotify_init failed: " << errno << "\n";
            return;
        }
        int wd = inotify_add_watch(fd, requirementsDir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
        if (wd < 0) {
            std::cerr << "[Watcher] inotify_add_watch failed for " << requirementsDir << ": " << errno << "\n";
            close(fd);
            return;
        }

        constexpr size_t BufLen = 16 * 1024;
        std::vector<char> buf(BufLen);

        std::cout << "[Watcher] Watching '" << requirementsDir << "' for *.md changes...\n";
        while (true) {
            int len = read(fd, buf.data(), (int)buf.size());
            if (len <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                continue;
            }
            int i = 0;
            while (i < len) {
                inotify_event* ev = (inotify_event*)&buf[i];
                if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)) && (ev->len > 0)) {
                    std::string fname(ev->name);
                    if (fname.size() >= 3 && fname.rfind(".md") == fname.size() - 3) {
                        fs::path p = fs::path(requirementsDir) / fname;
                        // small debounce delay
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        generateFromMd(p);
                    }
                }
                i += sizeof(inotify_event) + ev->len;
            }
        }

        // Unreachable in current design
        // inotify_rm_watch(fd, wd);
        // close(fd);
    }).detach();
}
