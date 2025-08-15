#include <crow.h>
#include "SchemaManager.h"
#include "IndexManager.h"
#include "DBManager.h"
#include "QueryExecutor.h"
#include "JwtUtils.h"
#include "authmiddleware.h"
#include "llm_mistral.h"
#include <fstream>

using namespace v8;

// LUC: 20210733 : ugliest of hacks
int crow::detail::dumb_timer_queue::tick = 5;

// Globals for V8
std::unique_ptr<Platform> g_platform;
Isolate* g_isolate;

// In your global scope:
v8::Persistent<v8::Context> persistent_ctx;


// In-process cache for require()
static std::unordered_map<std::string, Global<Object>> moduleCache;

// Load a file into a string
static std::string LoadScript(const std::string& path) {
    std::ifstream file(path);
    return { std::istreambuf_iterator<char>(file), {} };
}

// Converts a QueryResult into a crow::json::wvalue
inline crow::json::wvalue queryResultToJson(const QueryResult& qr) {
    crow::json::wvalue x;
    
    // Fill rows[] array
    for (size_t i = 0; i < qr.rows.size(); ++i) {
        const auto& row = qr.rows[i];
        crow::json::wvalue obj;
        for (const auto& kv : row.vals) {
            obj[kv.first] = kv.second;
        }
        // operator[](size_t) auto-grows into a list
        x[i] = std::move(obj);
    }
   
    return x;
}

// Initialize V8
/*static void InitializeV8() {
    V8::InitializeICUDefaultLocation("");
    V8::InitializeExternalStartupData("");
    g_platform = platform::NewDefaultPlatform();
    V8::InitializePlatform(g_platform.get());
    V8::Initialize();

    ArrayBuffer::Allocator* alloc = ArrayBuffer::Allocator::NewDefaultAllocator();
    Isolate::CreateParams params;
    params.array_buffer_allocator = alloc;
    g_isolate = Isolate::New(params);
}*/


//----------------------------------------------
// 1) CommonJS-style require()
//----------------------------------------------

/*static void RequireCallback(const FunctionCallbackInfo<Value>& info) {
    Isolate* iso = info.GetIsolate();
    HandleScope hs(iso);
    Local<Context> ctx = iso->GetCurrentContext();

    std::string name = *String::Utf8Value(iso, info[0]);
    if (moduleCache.count(name)) {
        info.GetReturnValue().Set(moduleCache[name].Get(iso));
        return;
    }

    std::string src = LoadScript("scripts/" + name + ".js");
    std::string wrapped = "(function(exports, require){" + src + "\n})";
    Local<String> code = String::NewFromUtf8(iso, wrapped.c_str(), NewStringType::kNormal).ToLocalChecked();
    Local<Script> scr = Script::Compile(ctx, code).ToLocalChecked();
    Local<Function> fn = scr->Run(ctx).ToLocalChecked().As<Function>();

    Local<Object> exports = Object::New(iso);
    Local<Value> args[2] = { exports, info.Callee() };
    fn->Call(ctx, ctx->Global(), 2, args).ToLocalChecked();

    moduleCache[name].Reset(iso, exports);
    info.GetReturnValue().Set(exports);
}*/

// �� RequireCallback ��
static void RequireCallback(const FunctionCallbackInfo<Value>& info) {
    Isolate* iso = info.GetIsolate();
    HandleScope hs(iso);
    Local<Context> ctx = iso->GetCurrentContext();

    if (info.Length() < 1 || !info[0]->IsString()) {
        iso->ThrowException(String::NewFromUtf8(iso, "require(name) expects a string", NewStringType::kNormal).ToLocalChecked());
        return;
    }

    std::string name = *String::Utf8Value(iso, info[0]);
    std::cerr << "[require] name=" << name << "\n";
    if (moduleCache.count(name)) {
        std::cerr << "[require] cache hit: " << name << "\n";
        info.GetReturnValue().Set(moduleCache[name].Get(iso));
        return;
    }

    std::string src = LoadScript("scripts/" + name + ".js");
    if (src.empty()) {
        std::cerr << "[require] empty source for: scripts/" << name << ".js\n";
    }
    std::string wrapped = "(function(exports, require){\n" + src + "\n})";
    Local<String> code = String::NewFromUtf8(iso, wrapped.c_str(), NewStringType::kNormal).ToLocalChecked();

    TryCatch tc(iso);
    Local<Script> scr;
    if (!Script::Compile(ctx, code).ToLocal(&scr)) {
        String::Utf8Value emsg(iso, tc.Exception());
        std::string msg = *emsg ? *emsg : "Compile error";
        std::cerr << "[require] compile failed for: " << name << " | " << msg << "\n";
        iso->ThrowException(String::NewFromUtf8(iso, msg.c_str(), NewStringType::kNormal).ToLocalChecked());
        return;
    }
    Local<Value> fnVal;
    if (!scr->Run(ctx).ToLocal(&fnVal) || !fnVal->IsFunction()) {
        String::Utf8Value emsg(iso, tc.Exception());
        std::string msg = *emsg ? *emsg : "Run error";
        iso->ThrowException(String::NewFromUtf8(iso, msg.c_str(), NewStringType::kNormal).ToLocalChecked());
        return;
    }
    Local<Function> fn = fnVal.As<Function>();

    Local<Object> exports = Object::New(iso);

    // Grab global require to pass through
    Local<Value> reqVal;
    if (!ctx->Global()->Get(ctx, String::NewFromUtf8(iso, "require", NewStringType::kNormal).ToLocalChecked()).ToLocal(&reqVal)) {
        reqVal = Undefined(iso);
    }

    Local<Value> args[2] = { exports, reqVal };
    Local<Value> ignored;
    if (!fn->Call(ctx, ctx->Global(), 2, args).ToLocal(&ignored)) {
        // propagate the exception to JS caller so it surfaces
        if (tc.HasCaught()) {
            String::Utf8Value emsg(iso, tc.Exception());
            std::string msg = *emsg ? *emsg : "Run error";
            std::cerr << "[require] call exception for: " << name << " | " << msg << "\n";
            iso->ThrowException(tc.Exception());
        }
        std::cerr << "[require] call failed for: " << name << "\n";
        return;
    }

    moduleCache[name].Reset(iso, exports);
    info.GetReturnValue().Set(exports);
    std::cerr << "[require] loaded: " << name << "\n";
}

// jwt binding
// -----------------------------------------------------------------------------
// 1) Your two native callbacks (once, near the top of main.cpp)
// -----------------------------------------------------------------------------
static void JsCppSignJwt(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* iso = info.GetIsolate();
    v8::HandleScope hs(iso);
    if (info.Length() != 2 || !info[0]->IsString() || !info[1]->IsString()) {
        iso->ThrowException(
          v8::String::NewFromUtf8(iso,
            "CppSignJwt(payloadJson, secret) requires two strings",
            v8::NewStringType::kNormal).ToLocalChecked());
        return;
    }
    v8::String::Utf8Value pj(iso, info[0]);  
    v8::String::Utf8Value sk(iso, info[1]);
    try {
        std::string token = CppSignJwt(std::string(*pj), std::string(*sk));
        info.GetReturnValue().Set(
          v8::String::NewFromUtf8(iso, token.c_str(),
            v8::NewStringType::kNormal).ToLocalChecked());
    } catch (const std::exception& e) {
        iso->ThrowException(
          v8::String::NewFromUtf8(iso, e.what(),
            v8::NewStringType::kNormal).ToLocalChecked());
    }
}

static void JsCppVerifyJwt(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* iso = info.GetIsolate();
    v8::HandleScope hs(iso);
    if (info.Length() != 2 || !info[0]->IsString() || !info[1]->IsString()) {
        iso->ThrowException(
          v8::String::NewFromUtf8(iso,
            "CppVerifyJwt(token, secret) requires two strings",
            v8::NewStringType::kNormal).ToLocalChecked());
        return;
    }
    v8::String::Utf8Value tk(iso, info[0]);
    v8::String::Utf8Value sk(iso, info[1]);
    try {
        std::string payload = CppVerifyJwt(std::string(*tk), std::string(*sk));
        info.GetReturnValue().Set(
          v8::String::NewFromUtf8(iso, payload.c_str(),
            v8::NewStringType::kNormal).ToLocalChecked());
    } catch (const std::exception& e) {
        iso->ThrowException(
          v8::String::NewFromUtf8(iso, e.what(),
            v8::NewStringType::kNormal).ToLocalChecked());
    }
}

// -----------------------------------------------------------------------------
// 2) The binding function
// -----------------------------------------------------------------------------
static void BindJwtUtils(v8::Isolate* iso, v8::Local<v8::Context> ctx) {
    using namespace v8;

    // CppSignJwt
    Local<FunctionTemplate> signTpl = FunctionTemplate::New(iso, JsCppSignJwt);
    Local<Function> signFn = signTpl->GetFunction(ctx).ToLocalChecked();
    ctx->Global()
       ->Set(ctx,
             String::NewFromUtf8(iso, "CppSignJwt", NewStringType::kNormal)
                 .ToLocalChecked(),
             signFn)
       .FromJust();

    // CppVerifyJwt
    Local<FunctionTemplate> verifyTpl = FunctionTemplate::New(iso, JsCppVerifyJwt);
    Local<Function> verifyFn = verifyTpl->GetFunction(ctx).ToLocalChecked();
    ctx->Global()
       ->Set(ctx,
             String::NewFromUtf8(iso, "CppVerifyJwt", NewStringType::kNormal)
                 .ToLocalChecked(),
             verifyFn)
       .FromJust();
}

///

static void BindRequire(Isolate* iso, Local<Context> ctx) {
    Local<FunctionTemplate> tpl = FunctionTemplate::New(iso, RequireCallback);
    Local<Function> requireFn = tpl->GetFunction(ctx).ToLocalChecked();
    Maybe<bool> ok = ctx->Global()
       ->Set(ctx,
             String::NewFromUtf8(iso, "require", NewStringType::kNormal).ToLocalChecked(),
             requireFn);
       
}

//----------------------------------------------
// 2) db.query / db.execute bindings
//----------------------------------------------
static void JsDbQuery(const FunctionCallbackInfo<Value>& info) {
    Isolate* iso = info.GetIsolate();
    HandleScope hs(iso);
    Local<Context> ctx = iso->GetCurrentContext();

    if (info.Length()<1 || !info[0]->IsString()) {
        iso->ThrowException(String::NewFromUtf8(iso,"db.query(sql) requires string",NewStringType::kNormal).ToLocalChecked());
        return;
    }
    std::string sql = *String::Utf8Value(iso, info[0]);
    //auto q = SqlParser::parse(sql);
    QueryResult r;
	QueryExecutor::execute(sql, r);
	auto result = queryResultToJson(r);
	
    std::string out = crow::json::dump(result);
    std::cout << "-------" << out << "---------" << std::endl;
    Local<Value> jsonV8 = JSON::Parse(ctx,
        String::NewFromUtf8(iso, out.c_str(), NewStringType::kNormal).ToLocalChecked()
    ).ToLocalChecked();
    info.GetReturnValue().Set(jsonV8);
}

static void JsDbExecute(const FunctionCallbackInfo<Value>& info) {
    Isolate* iso = info.GetIsolate();
    HandleScope hs(iso);
    Local<Context> ctx = iso->GetCurrentContext();

    if (info.Length()<1 || !info[0]->IsString()) {
        iso->ThrowException(String::NewFromUtf8(iso,"db.execute(sql) requires string",NewStringType::kNormal).ToLocalChecked());
        return;
    }
    std::string sql = *String::Utf8Value(iso, info[0]);
    try {
        //auto q = SqlParser::parse(sql);
        QueryResult r;
		QueryExecutor::execute(sql, r);
        Local<Object> obj = Object::New(iso);
        Maybe<bool> ok = obj->Set(ctx,
                 String::NewFromUtf8(iso,"success",NewStringType::kNormal).ToLocalChecked(),
                 Boolean::New(iso,true));
        info.GetReturnValue().Set(obj);
    } catch (const std::exception& e) {
        Local<Object> obj = Object::New(iso);
        Maybe<bool> ok = obj->Set(ctx,
                 String::NewFromUtf8(iso,"success",NewStringType::kNormal).ToLocalChecked(),
                 Boolean::New(iso,false));
                 
        Maybe<bool> err = obj->Set(ctx,
                 String::NewFromUtf8(iso,"error",NewStringType::kNormal).ToLocalChecked(),
                 String::NewFromUtf8(iso,e.what(),NewStringType::kNormal).ToLocalChecked());

        info.GetReturnValue().Set(obj);
    }
}

static void BindDbObject(Isolate* iso, Local<Context> ctx) {
    Isolate::Scope iscope(iso);
    HandleScope hs(iso);

    Local<ObjectTemplate> tpl = ObjectTemplate::New(iso);
    tpl->Set(String::NewFromUtf8(iso,"query",NewStringType::kNormal).ToLocalChecked(),
             FunctionTemplate::New(iso, JsDbQuery));
    tpl->Set(String::NewFromUtf8(iso,"execute",NewStringType::kNormal).ToLocalChecked(),
             FunctionTemplate::New(iso, JsDbExecute));

    // Direct RocksDB key-value accessors (for blockchain module)
    auto JsDbKvPut = [](const FunctionCallbackInfo<Value>& info){
        Isolate* iso = info.GetIsolate();
        HandleScope hs(iso);
        if (info.Length() < 3 || !info[0]->IsString() || !info[1]->IsString() || !info[2]->IsString()) {
            iso->ThrowException(String::NewFromUtf8(iso, "db.kvPut(cf,key,val) requires 3 strings", NewStringType::kNormal).ToLocalChecked());
            return;
        }
        std::string cf   = *String::Utf8Value(iso, info[0]);
        std::string key  = *String::Utf8Value(iso, info[1]);
        std::string val  = *String::Utf8Value(iso, info[2]);
        auto* handle = DBManager::instance().cf(cf);
        if (!handle) {
            iso->ThrowException(String::NewFromUtf8(iso, "Unknown column family", NewStringType::kNormal).ToLocalChecked());
            return;
        }
        auto s = DBManager::instance().db()->Put(rocksdb::WriteOptions(), handle, key, val);
        info.GetReturnValue().Set(Boolean::New(iso, s.ok()));
    };

    auto JsDbKvGet = [](const FunctionCallbackInfo<Value>& info){
        Isolate* iso = info.GetIsolate();
        HandleScope hs(iso);
        if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsString()) {
            iso->ThrowException(String::NewFromUtf8(iso, "db.kvGet(cf,key) requires 2 strings", NewStringType::kNormal).ToLocalChecked());
            return;
        }
        std::string cf   = *String::Utf8Value(iso, info[0]);
        std::string key  = *String::Utf8Value(iso, info[1]);
        auto* handle = DBManager::instance().cf(cf);
        if (!handle) {
            iso->ThrowException(String::NewFromUtf8(iso, "Unknown column family", NewStringType::kNormal).ToLocalChecked());
            return;
        }
        std::string val;
        auto s = DBManager::instance().db()->Get(rocksdb::ReadOptions(), handle, key, &val);
        if (!s.ok()) val.clear();
        info.GetReturnValue().Set(String::NewFromUtf8(iso, val.c_str(), NewStringType::kNormal).ToLocalChecked());
    };

    auto JsDbKvDel = [](const FunctionCallbackInfo<Value>& info){
        Isolate* iso = info.GetIsolate();
        HandleScope hs(iso);
        if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsString()) {
            iso->ThrowException(String::NewFromUtf8(iso, "db.kvDel(cf,key) requires 2 strings", NewStringType::kNormal).ToLocalChecked());
            return;
        }
        std::string cf   = *String::Utf8Value(iso, info[0]);
        std::string key  = *String::Utf8Value(iso, info[1]);
        auto* handle = DBManager::instance().cf(cf);
        if (!handle) {
            iso->ThrowException(String::NewFromUtf8(iso, "Unknown column family", NewStringType::kNormal).ToLocalChecked());
            return;
        }
        auto s = DBManager::instance().db()->Delete(rocksdb::WriteOptions(), handle, key);
        info.GetReturnValue().Set(Boolean::New(iso, s.ok()));
    };

    auto JsDbKvKeys = [](const FunctionCallbackInfo<Value>& info){
        Isolate* iso = info.GetIsolate();
        HandleScope hs(iso);
        Local<Context> ctx = iso->GetCurrentContext();
        if (info.Length() < 1 || !info[0]->IsString()) {
            iso->ThrowException(String::NewFromUtf8(iso, "db.kvKeys(cf[,prefix]) requires cf string", NewStringType::kNormal).ToLocalChecked());
            return;
        }
        std::string cf   = *String::Utf8Value(iso, info[0]);
        std::string prefix = (info.Length() >= 2 && info[1]->IsString()) ? *String::Utf8Value(iso, info[1]) : std::string();
        auto* handle = DBManager::instance().cf(cf);
        if (!handle) {
            iso->ThrowException(String::NewFromUtf8(iso, "Unknown column family", NewStringType::kNormal).ToLocalChecked());
            return;
        }
        auto it = std::unique_ptr<rocksdb::Iterator>(DBManager::instance().db()->NewIterator(rocksdb::ReadOptions(), handle));
        Local<Array> arr = Array::New(iso);
        uint32_t idx = 0;
        if (prefix.empty()) {
            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                auto k = it->key().ToString();
                arr->Set(ctx, idx++, String::NewFromUtf8(iso, k.c_str(), NewStringType::kNormal).ToLocalChecked()).FromJust();
            }
        } else {
            for (it->Seek(prefix); it->Valid(); it->Next()) {
                auto k = it->key().ToString();
                if (k.rfind(prefix, 0) != 0) break;
                arr->Set(ctx, idx++, String::NewFromUtf8(iso, k.c_str(), NewStringType::kNormal).ToLocalChecked()).FromJust();
            }
        }
        info.GetReturnValue().Set(arr);
    };

    tpl->Set(String::NewFromUtf8(iso,"kvPut",NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(iso, JsDbKvPut));
    tpl->Set(String::NewFromUtf8(iso,"kvGet",NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(iso, JsDbKvGet));
    tpl->Set(String::NewFromUtf8(iso,"kvDel",NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(iso, JsDbKvDel));
    tpl->Set(String::NewFromUtf8(iso,"kvKeys",NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(iso, JsDbKvKeys));

    Local<Object> dbObj = tpl->NewInstance(ctx).ToLocalChecked();
    Maybe<bool> ok = ctx->Global()
       ->Set(ctx,
             String::NewFromUtf8(iso, "db", NewStringType::kNormal).ToLocalChecked(),
             dbObj);
}

//----------------------------------------------
// 3) Invoke JS API function
//----------------------------------------------

static v8::Local<v8::Value> InvokeApiFunction(v8::Isolate* iso,
                                              v8::Local<v8::Context> ctx,
                                              const std::string& fnName,
                                              const crow::json::rvalue& args) {
    v8::Isolate::Scope iscope(iso);
    v8::HandleScope    hs(iso);
    v8::TryCatch       tc(iso);

    // 1) Grab global `api`
    v8::Local<v8::Value> apiVal;
    if (!ctx->Global()
             ->Get(ctx,
                   v8::String::NewFromUtf8(iso, "api", v8::NewStringType::kNormal)
                       .ToLocalChecked())
             .ToLocal(&apiVal) ||
        !apiVal->IsObject()) {
        std::cerr << "[InvokeApi] ERROR: missing global api object\n";
        return v8::Undefined(iso);
    }
    auto apiObj = apiVal.As<v8::Object>();

    // 2) api[fnName]
    v8::Local<v8::Value> fnVal;
    if (!apiObj
             ->Get(ctx,
                   v8::String::NewFromUtf8(iso, fnName.c_str(),
                                           v8::NewStringType::kNormal)
                       .ToLocalChecked())
             .ToLocal(&fnVal) ||
        !fnVal->IsObject()) {
        std::cerr << "[InvokeApi] ERROR: api." << fnName << " not found\n";
        return v8::Undefined(iso);
    }
    auto fnObj = fnVal.As<v8::Object>();

    // 3) fnObj.handler
    v8::Local<v8::Value> hVal;
    if (!fnObj
             ->Get(ctx,
                   v8::String::NewFromUtf8(iso, "handler",
                                           v8::NewStringType::kNormal)
                       .ToLocalChecked())
             .ToLocal(&hVal) ||
        !hVal->IsFunction()) {
        std::cerr << "[InvokeApi] ERROR: missing handler for api." << fnName << "\n";
        return v8::Undefined(iso);
    }
    auto handler = hVal.As<v8::Function>();

    // 4) Parse input JSON, fallback to `{}` on failure
    std::string paramJson = crow::json::dump(args);
    auto jsonStr = v8::String::NewFromUtf8(iso,
                                           paramJson.c_str(),
                                           v8::NewStringType::kNormal)
                       .ToLocalChecked();
    v8::Local<v8::Value> paramV8;
    if (!v8::JSON::Parse(ctx, jsonStr).ToLocal(&paramV8)) {
        std::cerr << "[InvokeApi] WARNING: JSON.parse failed for '"
                  << paramJson << "'; using empty object\n";
        paramV8 = v8::Object::New(iso);
    }

    // 5) Call the handler
    v8::Local<v8::Value> result;
    if (!handler->Call(ctx, fnObj, 1, &paramV8).ToLocal(&result)) {
        // Log the exception message and any location info
        v8::String::Utf8Value exRaw(iso, tc.Exception());
        std::string rawMsg = *exRaw ? *exRaw : "<no exception>";
        std::string detail;
        v8::Local<v8::Message> message = tc.Message();
        if (!message.IsEmpty()) {
            v8::String::Utf8Value msgStr(iso, message->Get());
            int line = message->GetLineNumber(ctx).FromJust();
            int col  = message->GetStartColumn();
            detail = std::string(*msgStr) + " (line " +
                     std::to_string(line) + ", col " +
                     std::to_string(col) + ")";
        }
        std::cerr << "[InvokeApi] JS exception in api." << fnName
                  << " -> " << rawMsg
                  << (detail.empty() ? "" : (" | " + detail))
                  << "\n";
        return v8::Undefined(iso);
    }

    return result;
}

// Call the JS sanitize.safeStringify(value) helper and return its result as a std::string.
// On any error (missing module, exception, etc.) it returns an empty string.
static std::string SafeStringify(v8::Isolate* isolate,
                                 v8::Local<v8::Context> ctx,
                                 v8::Local<v8::Value> element) {
  	std::cout << "safe sanitize" << std::endl;
    // 1) Create a local handle-scope for any temporaries we make
    v8::HandleScope hs(isolate);

    // 2) Grab the sanitize module from global: `const sanitize = globalThis.sanitize;`
    v8::Local<v8::Value> modVal;
    if (!ctx->Global()
             ->Get(ctx,
                   v8::String::NewFromUtf8(isolate, "sanitize",
                                           v8::NewStringType::kNormal)
                       .ToLocalChecked())
             .ToLocal(&modVal) ||
        !modVal->IsObject()) {
        std::cerr << "[SafeStringify] sanitize module not found\n";
        return "";
    }
    auto modObj = modVal.As<v8::Object>();

    // 3) Grab the safeStringify function: `const fn = sanitize.safeStringify;`
    v8::Local<v8::Value> fnVal;
    if (!modObj
             ->Get(ctx,
                   v8::String::NewFromUtf8(isolate, "safeStringify",
                                           v8::NewStringType::kNormal)
                       .ToLocalChecked())
             .ToLocal(&fnVal) ||
        !fnVal->IsFunction()) {
        std::cerr << "[SafeStringify] sanitize.safeStringify not found\n";
        return "";
    }
    auto stringifyFn = fnVal.As<v8::Function>();

    // 4) Call it with our element as the single argument
    v8::TryCatch tc(isolate);
    v8::Local<v8::Value> argv[1] = { element };
    v8::Local<v8::Value> strVal;
    if (!stringifyFn->Call(ctx, ctx->Global(), 1, argv).ToLocal(&strVal)) {
        v8::String::Utf8Value err(isolate, tc.Exception());
        std::cerr << "[SafeStringify] JS exception: "
                  << (*err ? *err : "<unknown>") << "\n";
        return "";
    }

    // 5) Convert the returned JS string into a std::string
    if (!strVal->IsString()) {
        std::cerr << "[SafeStringify] Unexpected return type; expected String\n";
        return "";
    }
    v8::String::Utf8Value utf8(isolate, strVal);
    return *utf8 ? *utf8 : "";
}

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
// simple helper to detect mimetype for filetypes
static std::string mime_for(std::string path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".js")                    return "text/javascript; charset=utf-8";
    if (ext == ".css")                   return "text/css; charset=utf-8";
    if (ext == ".json")                  return "application/json; charset=utf-8";
    if (ext == ".webmanifest")           return "application/manifest+json";
    if (ext == ".wasm")                  return "application/wasm";
    if (ext == ".svg")                   return "image/svg+xml";
    if (ext == ".png")                   return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif")                   return "image/gif";
    if (ext == ".ico")                   return "image/x-icon";
    if (ext == ".woff2")                 return "font/woff2";
    if (ext == ".woff")                  return "font/woff";
    if (ext == ".ttf")                   return "font/ttf";
    if (ext == ".map")                   return "application/json; charset=utf-8";
    return "application/octet-stream";
}

//----------------------------------------------
// 4) main()
//----------------------------------------------
int main(int argc, char** argv) {
	
	// --- V8 init ---
    //InitializeV8();
    V8::InitializeICUDefaultLocation("");
    V8::InitializeExternalStartupData("");
    g_platform = platform::NewDefaultPlatform();
    V8::InitializePlatform(g_platform.get());
    V8::Initialize();

    ArrayBuffer::Allocator* alloc = ArrayBuffer::Allocator::NewDefaultAllocator();
    Isolate::CreateParams params;
    params.array_buffer_allocator = alloc;
    g_isolate = Isolate::New(params);
    {
    	Isolate::Scope isolate_scope(g_isolate);
        HandleScope hs(g_isolate);
        
		// In main(), immediately after:
		Local<Context> ctx = Context::New(g_isolate);
		Context::Scope ctx_scope(ctx);
		persistent_ctx.Reset(g_isolate, ctx);

		// 1) Bind require() & db & jwt
        BindRequire(g_isolate, ctx);
        BindDbObject(g_isolate, ctx);
        BindJwtUtils(g_isolate, ctx);
        
        // 2.1) Grab the global �require�  
		v8::Local<v8::Value> require_val;
		if (!ctx->Global()
		        ->Get(ctx, v8::String::NewFromUtf8(g_isolate, "require",
		                                           v8::NewStringType::kNormal)
		                             .ToLocalChecked())
		        .ToLocal(&require_val) ||
		    !require_val->IsFunction()) {
			// handle the fact that �require� isn�t actually defined
		  	return -1;
		}
		v8::Local<v8::Function> requireFn = require_val.As<v8::Function>();

		// 2.2) Create the argument you actually want � a V8 string �auth�
		v8::Local<v8::String> auth_str =
		    v8::String::NewFromUtf8(g_isolate, "auth", v8::NewStringType::kNormal)
		        .ToLocalChecked();
		v8::Local<v8::Value> require_argv[] = { auth_str };
		
		// 2.3) Call require(auth)
		v8::MaybeLocal<v8::Value> maybe_mod =
		    requireFn->Call(ctx, ctx->Global(), 1, require_argv);
		v8::Local<v8::Value> authMod;
		if (!maybe_mod.ToLocal(&authMod)) {
		  // the require() threw an exception�handle it!
		  return 0;
		}

		// 2.4) Stash it back on the global as �auth�
		ctx->Global()
		    ->Set(ctx,
		          v8::String::NewFromUtf8(g_isolate, "auth",
		                                  v8::NewStringType::kNormal)
		              .ToLocalChecked(),
		          authMod)
		    .FromJust();

	
		// ��� 2.5) require("sanitize") and stash it as global �sanitize� ������������
		v8::Local<v8::String> sanitize_str =
		    v8::String::NewFromUtf8(g_isolate, "sanitize", v8::NewStringType::kNormal)
		        .ToLocalChecked();
		v8::Local<v8::Value> require_argv2[] = { sanitize_str };
		
		v8::Local<v8::Value> sanitizeModVal;
		if (!requireFn->Call(ctx, ctx->Global(), 1, require_argv2)
		         .ToLocal(&sanitizeModVal) ||
		    !sanitizeModVal->IsObject()) {
		    std::cerr << "[Init] ERROR: require(\"sanitize\") failed\n";
		    return -1;  // or handle error
		}
		
		// stash it on global as `sanitize`
		ctx->Global()
		    ->Set(ctx,
		          sanitize_str,
		          sanitizeModVal)
		    .FromJust();

		// 3) Load main business file (defines api.login, api.list, etc.)
        std::string biz = LoadScript("scripts/business.js");
		/*Local<Script> bs = Script::Compile(
            ctx,
            String::NewFromUtf8(g_isolate,biz.c_str(),NewStringType::kNormal).ToLocalChecked()
        ).ToLocalChecked();
        bs->Run(ctx).ToLocalChecked();*/
        
        TryCatch trycatch(g_isolate);
		Local<String> source = String::NewFromUtf8(g_isolate, biz.c_str(), NewStringType::kNormal).ToLocalChecked();

		Local<Script> bs;
		if (!Script::Compile(ctx, source).ToLocal(&bs)) {
    		String::Utf8Value err(g_isolate, trycatch.Exception());
    		std::cerr << "[V8 COMPILE ERROR] " << *err << std::endl;
    		return 1;
		}	
		
		Local<Value> result;
		if (!bs->Run(ctx).ToLocal(&result)) {
		    String::Utf8Value err(g_isolate, trycatch.Exception());
		    std::cerr << "[V8 RUNTIME ERROR] " << *err << std::endl;
		    return 1;
		}
        
   	} // V8 engine initial scope ends
    
	
    crow::App<AuthMiddleware> app;
    // Configure whether to allow JWT via query param (?token=)
    // Set environment variable WS_ALLOW_QUERY_TOKEN=0 or 'false' to disable.
    if (const char* env = std::getenv("WS_ALLOW_QUERY_TOKEN")) {
        std::string v(env);
        if (v == "0" || v == "false" || v == "FALSE") {
            AuthMiddleware::SetQueryTokenFallback(false);
        }
    }

	
	// --- Schema & DB init ---
	SchemaManager::loadFromFile("schemas.json");
	if (!DBManager::instance().init("quarks_db")) return 1;
	IndexManager::rebuildAll();
	
	// ---------- Crow route wiring for LLM ----------
    /*CROW_ROUTE(app, "/llm/generateCode")
	    .methods("POST"_method)
    ([](const crow::request& req){
    	
    	std::cout << "llm" << std::endl;
    
    	curl_global_init(CURL_GLOBAL_DEFAULT);

	    LLMConfig cfg;
	    // Set one of these:
	    // cfg.PROXY_URL = "https://your-proxy.example/generate";           // preferred
	    // or direct:
	    cfg.OPENROUTER_API_KEY = "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";
		                     // keep server-side!
	
	    try {
            auto body = json::parse(req.body);

            std::string requirements = body["requirements"].get<std::string>();
            std::string model = body.value("model", std::string("qwen/qwen3-30b-a3b:free"));
            bool stream = body.value("stream", false);
            
            std::cout << requirements << model << stream << std::endl;

            std::string code = generateCodeLLM(cfg, requirements, model, stream);

            json out = { {"ok", true}, {"code", code} };
            crow::response r{ out.dump() };
            r.set_header("Content-Type", "application/json; charset=utf-8");
            return r;
        } catch (const std::exception& e) {
            json err = { {"ok", false}, {"error", e.what()} };
            crow::response r{ 500, err.dump() };
            r.set_header("Content-Type", "application/json; charset=utf-8");
            return r;
        }
        
        curl_global_cleanup();
    });*/
	

	CROW_ROUTE(app, "/api/<string>")
	    .methods("POST"_method)
	([&](const crow::request& req, crow::response& res, std::string fn) {
	    // 1) Parse JSON input, default to {}
	    auto body = crow::json::load(req.body);
	    if (!body || body.t() != crow::json::type::Object)
	        body = crow::json::load("{}");
	
	    // �� V8 THREAD SETUP ��
	    v8::Locker        locker(g_isolate);
	    v8::Isolate::Scope iscope(g_isolate);
	    v8::HandleScope   hs(g_isolate);
	
	    v8::Local<v8::Context> ctx = persistent_ctx.Get(g_isolate);
	    
		//----Inner Context Scope (�new scope�) ����
		v8::Context::Scope context_scope(ctx);
		// ������������������
	
	    // 2) Invoke your safe API function under TryCatch
	    v8::TryCatch tc(g_isolate);
	    v8::Local<v8::Value> result = InvokeApiFunction(g_isolate, ctx, fn, body);
	
	    // If the JS handler itself threw, catch it:
	    if (tc.HasCaught()) {
	        v8::String::Utf8Value err(g_isolate, tc.Exception());
	        res.code = 500;
	        return res.end(std::string("JS handler exception: ") + *err);
	    }
	    
	    // 3) If handler returned undefined, 404 
	    if (result->IsUndefined()) {
	        res.code = 404;
	        return res.end("API handler undefined or error");
	    }
	    
	    // 4) Safely JSON.stringify the result
	    std::string s;
		s = SafeStringify(g_isolate, ctx, result);	
		std::cout << "got result: " << s << std::endl;
	
		res.set_header("Content-Type", "application/json");
		return res.end(s);

	});
    
    /////////
    
    // 6) Default page provider 
    crow::mustache::set_base("./public/");
	CROW_ROUTE(app, "/")([]() {
        crow::mustache::context ctx;
        ctx["name"] = "Crow User";
        auto page = crow::mustache::load("index.html");
        return page.render(ctx);
    });
	/*CROW_ROUTE(app, "/public/<path>")
	([](std::string p) {
		crow::mustache::context ctx;
        ctx["name"] = "Crow User";
        auto page = crow::mustache::load(p);
        return page.render(ctx);
    });*/
    
    CROW_ROUTE(app, "/public/<path>")
	([](std::string p) {
	    // guard + directory default
	    if (p.find("..") != std::string::npos) return crow::response(404);
	    if (p.empty() || p.back() == '/') p += "index.html";
	
	    const std::string mime = mime_for(p);
	
	    // HTML via mustache (uses crow::mustache::set_base("public") you set earlier)
	    if (mime.rfind("text/html", 0) == 0) {
	        crow::mustache::context ctx;
	        ctx["name"] = "Crow User";
	        auto body = crow::mustache::load(p).render(ctx);
	
	        crow::response r(body);
	        r.set_header("Content-Type", mime);
	        return r;
	    }
	
	    // everything else served raw from ./public
	    std::ifstream f((fs::path("public") / p).string(), std::ios::binary);
	    if (!f.good()) return crow::response(404);
	
	    std::ostringstream ss; ss << f.rdbuf();
	    crow::response r(ss.str());
	    r.set_header("Content-Type", mime);
	    return r;
	});
    
	// Wire routes (works with any crow::App<...>)
	std::string llm_key = "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";
	Mistral::SetupRoutes(&app, std::string(llm_key));
	
	/*try {
        Mistral::TestCall(llm_key);
    } catch (const std::exception& e) {
        std::cerr << "Error calling Mistral: " << e.what() << "\n";
        return 2;
    }*/

    app.port(18080).multithreaded().run();


	// OPTIONAL, to be extra-sure there really are no left-over scopes (may cause core dump)
 	//g_isolate->Exit();

	// --- Cleanup V8 --- (will cause core dump)
    //g_isolate->Dispose();
        
    V8::Dispose();
    V8::ShutdownPlatform();
	
	return 0;
}
