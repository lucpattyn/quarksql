#pragma once

#include <crow.h>

#include <v8.h>
#include <libplatform/libplatform.h>

extern v8::Isolate* g_isolate;
extern v8::Persistent<v8::Context> persistent_ctx;

struct AuthMiddleware {
    struct context {};

    // Controls whether the middleware accepts JWT via `?token=` query param
    // in addition to Authorization or Sec-WebSocket-Protocol.
    inline static bool allow_query_token_fallback = true;
    static void SetQueryTokenFallback(bool enabled) { allow_query_token_fallback = enabled; }
    static bool GetQueryTokenFallback() { return allow_query_token_fallback; }

    void before_handle(crow::request& req, crow::response& res, context&) {
        // 1) Allow public endpoints without auth
        if (req.url == "/" || req.url == "/api/login" || req.url == "/api/signup" ||
            req.url.rfind("/public/", 0) == 0 || req.url.rfind("/ask", 0) == 0) {
            return;
        }

        // 2) Extract token: Prefer Authorization header; then Sec-WebSocket-Protocol (subprotocol); (optional) fallback to ?token=
        std::string token;
        auto auth = req.get_header_value("Authorization");
        if (!auth.empty() && auth.rfind("Bearer ", 0) == 0) {
            token = auth.substr(7);
        }
        if (token.empty()) {
            // Parse Sec-WebSocket-Protocol: may be a comma-separated list, e.g. "jwt, <token>"
            auto proto = req.get_header_value("Sec-WebSocket-Protocol");
            if (!proto.empty()) {
                std::vector<std::string> parts; parts.reserve(3);
                size_t start = 0;
                while (start < proto.size()) {
                    size_t pos = proto.find(',', start);
                    std::string seg = (pos==std::string::npos) ? proto.substr(start) : proto.substr(start, pos-start);
                    // trim whitespace
                    auto l = seg.find_first_not_of(" \t");
                    auto r = seg.find_last_not_of(" \t");
                    if (l!=std::string::npos) seg = seg.substr(l, r-l+1); else seg.clear();
                    if (!seg.empty()) parts.push_back(seg);
                    if (pos==std::string::npos) break; else start = pos+1;
                }
                if (!parts.empty()) {
                    if (parts[0] == "jwt" && parts.size() >= 2) {
                        token = parts[1];
                        res.add_header("Sec-WebSocket-Protocol", "jwt");
                    } else {
                        // Fallback: treat first token as the token and echo it back
                        token = parts[0];
                        res.add_header("Sec-WebSocket-Protocol", parts[0]);
                    }
                }
            }
        }
        if (token.empty() && allow_query_token_fallback) {
            // Optional fallback for non-browser clients (configurable)
            const char* qp = req.url_params.get("token");
            if (qp) token = qp;
        }
        if (token.empty()) { res.code = 401; return res.end("Missing token"); }

        // 3) Verify via JS auth.verify(token) in V8 context
        v8::Locker        locker(g_isolate);
        v8::Isolate::Scope iscope(g_isolate);
        v8::HandleScope   hs(g_isolate);
        v8::Local<v8::Context> _ctx = persistent_ctx.Get(g_isolate);
        v8::Context::Scope context_scope(_ctx);
        v8::TryCatch tc(g_isolate);

        // Lookup global "auth" object
        v8::Local<v8::Value> authVal;
        if (!_ctx->Global()->Get(_ctx,
              v8::String::NewFromUtf8(g_isolate, "auth", v8::NewStringType::kNormal).ToLocalChecked()
            ).ToLocal(&authVal) || !authVal->IsObject()) {
            res.code = 500; return res.end("Auth module missing");
        }
        v8::Local<v8::Object> authObj = authVal.As<v8::Object>();

        // Lookup auth.verify
        v8::Local<v8::Value> verifyVal;
        if (!authObj->Get(_ctx,
              v8::String::NewFromUtf8(g_isolate, "verify", v8::NewStringType::kNormal).ToLocalChecked()
            ).ToLocal(&verifyVal) || !verifyVal->IsFunction()) {
            res.code = 500; return res.end("Auth.verify not a function");
        }
        v8::Local<v8::Function> verifyFn = verifyVal.As<v8::Function>();

        // Call verify(token)
        v8::Local<v8::Value> arg = v8::String::NewFromUtf8(g_isolate, token.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
        v8::Local<v8::Value> result;
        if (!verifyFn->Call(_ctx, authObj, 1, &arg).ToLocal(&result)) {
            res.code = 401; return res.end("Invalid token");
        }
        // If needed, inspect `result` for claims
    }

    void after_handle(crow::request&, crow::response&, context&) {}
};
