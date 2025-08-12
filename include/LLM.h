// LLM.h

#pragma once 

#include <curl/curl.h>
#include <json.hpp>
#include <regex>
#include <sstream>

using json = nlohmann::json;

// ---------- Config ----------
struct LLMConfig {
    std::string PROXY_URL;            // e.g. "https://your-proxy.example/generate" (optional)
    std::string OPENROUTER_API_KEY;   // server-side only (optional)
    std::string OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions";
};

// ---------- Small utils ----------
static size_t writeToString(void* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* s = static_cast<std::string*>(userdata);
    s->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

struct HeaderCapture {
    std::string contentType;
    static size_t headerCb(char* buffer, size_t size, size_t nitems, void* userdata) {
        size_t len = size * nitems;
        HeaderCapture* h = static_cast<HeaderCapture*>(userdata);
        std::string hdr(buffer, len);
        // very simple parse
        if (hdr.rfind("Content-Type:", 0) == 0 || hdr.rfind("content-type:", 0) == 0) {
            auto pos = hdr.find(':');
            if (pos != std::string::npos) {
                h->contentType = hdr.substr(pos + 1);
                // trim
                while (!h->contentType.empty() && (h->contentType.front()==' '||h->contentType.front()=='\t')) h->contentType.erase(h->contentType.begin());
                while (!h->contentType.empty() && (h->contentType.back()=='\r'||h->contentType.back()=='\n'||h->contentType.back()==' ')) h->contentType.pop_back();
                // lowercase for checks
                for (auto& c: h->contentType) c = (char)std::tolower((unsigned char)c);
            }
        }
        return len;
    }
};

// ---------- Build payload ----------
static std::string buildPayload(const std::string& model,
                                bool stream,
                                const std::string& requirements)
{
    json j = {
        {"model", model},
        {"stream", stream},
        {"messages", json::array({
            json{
                {"role","system"},
                {"content",
                    std::string(
                        "You are a senior JavaScript engineer. "
                        "Convert the user requirements into modern, idiomatic JavaScript. "
                        "Return ONLY one fenced code block using language tag \"javascript\". "
                        "Include concise JSDoc and a usage example at the bottom. "
                        "Do not include explanations outside the code block."
                    )
                }
            },
            json{ {"role","user"}, {"content", requirements} }
        })}
    };
    return j.dump();
}

// ---------- HTTP POST (sync) ----------
static bool httpPostJson(const std::string& url,
                         const std::string& bearerAuth,  // empty to skip
                         const std::string& jsonBody,
                         long& httpCodeOut,
                         std::string& contentTypeOut,
                         std::string& bodyOut,
                         std::string& errOut)
{
    CURL* curl = curl_easy_init();
    if (!curl) { errOut = "curl_easy_init failed"; return false; }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string authHdr;
    if (!bearerAuth.empty()) {
        authHdr = "Authorization: Bearer " + bearerAuth;
        headers = curl_slist_append(headers, authHdr.c_str());
    }

    HeaderCapture hc;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bodyOut);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCapture::headerCb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hc);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        errOut = curl_easy_strerror(rc);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCodeOut);
    contentTypeOut = hc.contentType;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return true;
}

// ---------- SSE assembler (fallback if proxy sends SSE) ----------
static std::string assembleFromSSE(const std::string& sseText) {
    std::stringstream ss(sseText);
    std::string line;
    std::string assembled;

    while (std::getline(ss, line)) {
        if (line.rfind("data:", 0) != 0) continue;
        std::string payload = line.substr(5);
        // trim
        while (!payload.empty() && (payload.front()==' '||payload.front()=='\t')) payload.erase(payload.begin());
        if (payload == "[DONE]") continue;
        try {
            auto evt = json::parse(payload);
            if (evt.contains("choices") && evt["choices"].is_array() &&
                !evt["choices"].empty() &&
                evt["choices"][0].contains("delta") &&
                evt["choices"][0]["delta"].contains("content"))
            {
                auto d = evt["choices"][0]["delta"]["content"];
                if (d.is_string()) assembled += d.get<std::string>();
            }
        } catch (...) {
            // ignore malformed keep-alives
        }
    }
    return assembled;
}

// ---------- Extract fenced code ----------
static std::string extractCode(const std::string& textIn) {
    const std::string text = textIn;
    // ```lang\n...\n```
    static const std::regex block("```[ \\t]*([a-zA-Z0-9_-]+)?[ \\t]*\\n([\\s\\S]*?)```",
                                  std::regex::ECMAScript);
    std::smatch m;
    if (std::regex_search(text, m, block) && m.size() >= 3) {
        return m[2].str();
    }
    // ```...```
    static const std::regex inlineFence("```([\\s\\S]*?)```", std::regex::ECMAScript);
    if (std::regex_search(text, m, inlineFence) && m.size() >= 2) {
        return m[1].str();
    }
    // fallback whole text
    return text;
}

// ---------- Core: call LLM and return ONLY the code string ----------
static std::string generateCodeLLM(const LLMConfig& cfg,
                                   const std::string& requirements,
                                   const std::string& model,
                                   bool stream)
{
    // pick endpoint + auth
    std::string endpoint;
    std::string bearer;
    if (!cfg.PROXY_URL.empty()) {
        endpoint = cfg.PROXY_URL;         // proxy keeps your key safe
        bearer.clear();                   // proxy should handle auth
    } else {
        endpoint = cfg.OPENROUTER_URL;    // direct to OpenRouter
        bearer   = cfg.OPENROUTER_API_KEY;
        if (bearer.empty())
            throw std::runtime_error("Missing OPENROUTER_API_KEY");
    }

    // payload
    std::string payload = buildPayload(model.empty() ? "qwen/qwen3-30b-a3b:free" : model,
                                       stream,
                                       requirements);

    long httpCode = 0;
    std::string contentType, body, err;
    if (!httpPostJson(endpoint, bearer, payload, httpCode, contentType, body, err)) {
        throw std::runtime_error("HTTP error: " + err);
    }
    if (httpCode < 200 || httpCode >= 300) {
        std::ostringstream oss;
        oss << "Upstream " << httpCode << ": " << body;
        throw std::runtime_error(oss.str());
    }

    // SSE fallback if proxy sent event-stream even when stream=false
    if (contentType.find("text/event-stream") != std::string::npos) {
        std::string assembled = assembleFromSSE(body);
        return extractCode(assembled);
    }

    // Normal JSON (non-streaming)
    try {
        auto j = json::parse(body);

        // common shapes
        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
            const auto& msg = j["choices"][0]["message"];
            if (msg.is_object() && msg.contains("content") && msg["content"].is_string()) {
                return extractCode(msg["content"].get<std::string>());
            }
        }
        if (j.contains("code") && j["code"].is_string()) {
            return extractCode(j["code"].get<std::string>());
        }
        if (j.contains("content") && j["content"].is_string()) {
            return extractCode(j["content"].get<std::string>());
        }

        // last resort
        return extractCode(body);
    } catch (...) {
        // if JSON parse fails, try extracting from raw text
        return extractCode(body);
    }
}


