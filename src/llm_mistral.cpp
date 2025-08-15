#include "llm_mistral.h"

#include <crow.h>
#include "crow/websocket.h"

#include <curl/curl.h>
#include <json.hpp>

#include <cstdlib>
#include <string>
#include <stdexcept>
#include <iostream>

using json = nlohmann::json;

static const char* OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions";

// ---------- tiny libcurl helper for blocking POST ----------
namespace http {
  static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
  }

  inline std::string post_json_blocking(const std::string& url,
                                        const std::string& body,
                                        const std::string& bearer) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth = "Authorization: Bearer " + bearer;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "HTTP-Referer: http://localhost");
    headers = curl_slist_append(headers, "X-Title: QuarkSQL");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http::write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
      throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(res));
    }
    if (code < 200 || code >= 300) {
      throw std::runtime_error("HTTP error " + std::to_string(code) + " body: " + response);
    }
    return response;
  }
}

// ---------- route wiring in a generic function ----------
void Mistral::SetupRoutes(crow::App<AuthMiddleware>* crowApp, const std::string api_key) {
  crow::App<AuthMiddleware>& app = *crowApp;

  auto add_cors = [](crow::response& r) {
    r.add_header("Access-Control-Allow-Origin", "*");
    r.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    r.add_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  };
  auto add_sse_headers = [&](crow::response& r) {
    add_cors(r);
    r.add_header("Cache-Control", "no-cache");
    r.add_header("Connection", "keep-alive");
    r.add_header("Content-Type", "text/event-stream");
    r.add_header("X-Accel-Buffering", "no");
  };

  CROW_ROUTE(app, "/healthz").methods(crow::HTTPMethod::GET)([](){
    return crow::response(200, "ok");
  });

  // ---- Non-streaming: POST /ask  { "prompt": "...", "model": "mistral-small-latest" }
  CROW_ROUTE(app, "/ask").methods(crow::HTTPMethod::POST)([api_key, add_cors](const crow::request& req){
    crow::response r;
    try {
      json body = json::parse(req.body);
      std::string prompt = body.value("prompt", "");
      std::string model  = body.value("model", "mistral-small-latest");
      if (prompt.empty()) {
        r.code = 400; add_cors(r);
        r.add_header("Content-Type","application/json");
        r.write(R"({"error":"missing 'prompt'"})");
        return r;
      }

      json payload = {
        {"model", model},
        {"messages", json::array({ json{{"role","user"},{"content",prompt}} })},
        {"stream", false}
      };

      std::string resp = http::post_json_blocking(OPENROUTER_URL, payload.dump(), api_key);

      // Extract assistant content
      auto j = json::parse(resp);
      std::string out;
      if (j.contains("choices") && !j["choices"].empty()) {
        out = j["choices"][0]["message"]["content"].get<std::string>();
      }

      r.code = 200; add_cors(r);
      r.add_header("Content-Type","application/json");
      r.write(json{{"response", out}}.dump());
      return r;

    } catch (const std::exception& e) {
      r.code = 500; add_cors(r);
      r.add_header("Content-Type","application/json");
      r.write(std::string(R"({"error":")") + e.what() + R"("})");
      return r;
    }
  });

  // ---- Streaming SSE: POST /ask/stream
  // Proxies provider SSE to the browser as:  data: {"delta":"..."}\n\n
  CROW_ROUTE(app, "/ask/stream").methods(crow::HTTPMethod::POST)(
    [api_key, add_sse_headers](const crow::request& req, crow::response& res) {
      try {
        json body = json::parse(req.body);
        std::string prompt = body.value("prompt", "");
        std::string model  = body.value("model", "mistral-small-latest");
        if (prompt.empty()) {
          res.code = 400; add_sse_headers(res);
          res.end(R"({"error":"missing 'prompt'"})");
          return;
        }

        res.code = 200;
        add_sse_headers(res);

        CURL* curl = curl_easy_init();
        if (!curl) {
          res.write("data: {\"error\":\"curl init failed\"}\n\n");
          res.write("data: [DONE]\n\n");
          res.end();
          return;
        }

        json payload = {
          {"model", model},
          {"messages", json::array({ json{{"role","user"},{"content",prompt}} })},
          {"stream", true}
        };
        std::string body_str = payload.dump();

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, auth.c_str());
        headers = curl_slist_append(headers, "Accept: text/event-stream");
        headers = curl_slist_append(headers, "HTTP-Referer: http://localhost");
        headers = curl_slist_append(headers, "X-Title: QuarkSQL");

        // State for write callback
        struct State {
          crow::response* pres;
          std::string buffer;
        } st{&res, ""};

        auto write_cb = +[](char* ptr, size_t size, size_t nmemb, void* userdata)->size_t {
          auto* s = static_cast<State*>(userdata);
          size_t bytes = size * nmemb;
          s->buffer.append(ptr, bytes);

          // Robust SSE parsing: handle both \n\n and \r\n\r\n delimiters
          while (true) {
            size_t pos1 = s->buffer.find("\n\n");
            size_t pos2 = s->buffer.find("\r\n\r\n");
            size_t pos;
            size_t delimLen;
            if (pos1 == std::string::npos && pos2 == std::string::npos) break;
            if (pos2 == std::string::npos || (pos1 != std::string::npos && pos1 < pos2)) {
              pos = pos1; delimLen = 2;
            } else {
              pos = pos2; delimLen = 4;
            }

            std::string frame = s->buffer.substr(0, pos);
            s->buffer.erase(0, pos + delimLen);

            // Examine lines within the frame, looking for lines starting with "data:"
            size_t start = 0;
            while (start < frame.size()) {
              size_t end = frame.find_first_of("\n\r", start);
              std::string line = (end == std::string::npos) ? frame.substr(start) : frame.substr(start, end - start);
              if (end == std::string::npos) start = frame.size(); else {
                if (frame[end] == '\r' && end + 1 < frame.size() && frame[end+1] == '\n') start = end + 2; else start = end + 1;
              }

              if (line.rfind("data:", 0) == 0) {
                std::string payload = line.substr(5);
                if (!payload.empty() && payload[0]==' ') payload.erase(0,1);

                if (payload == "[DONE]") {
                  s->pres->write("data: [DONE]\n\n");
                  s->pres->end();
                  continue;
                }

                try {
                  auto j = json::parse(payload);
                  std::string delta;
                  if (j.contains("choices") &&
                      !j["choices"].empty() &&
                      j["choices"][0].contains("delta") &&
                      j["choices"][0]["delta"].contains("content") &&
                      !j["choices"][0]["delta"]["content"].is_null()) {
                    delta = j["choices"][0]["delta"]["content"].get<std::string>();
                  }
                  if (!delta.empty()) {
                    s->pres->write(std::string("data: {\"delta\":") + json(delta).dump() + "}\n\n");
                  }
                } catch (...) {
                  // Forward raw payload if not JSON
                  s->pres->write(std::string("data: ") + payload + "\n\n");
                }
              }
            }
          }
          return bytes;
        };

        curl_easy_setopt(curl, CURLOPT_URL, OPENROUTER_URL);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &st);

        CURLcode code = curl_easy_perform(curl);
        if (code != CURLE_OK) {
          // Stream may still be open — report error to client
          res.write(std::string("data: {\"error\":") + json(std::string("curl: ") + curl_easy_strerror(code)).dump() + "}\n\n");
          res.write("data: [DONE]\n\n");
          res.end();
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

      } catch (const std::exception& e) {
        res.code = 500; add_sse_headers(res);
        res.write(std::string("data: {\"error\":") + json(e.what()).dump() + "}\n\n");
        res.write("data: [DONE]\n\n");
        res.end();
      }
    }
  );

  // ---- WebSocket endpoint: /mistral/ws
  // Client sends JSON: {"prompt":"...","model":"...","stream":true|false}
  // If stream=true → stream {type:delta, delta:"..."} then {type:done}
  // If stream=false → single {type:final, text:"..."}
  CROW_ROUTE(app, "/mistral/ws").websocket()
  .onopen([](crow::websocket::connection& /*conn*/){
    // No-op; client should send a message with prompt/model
  })
  .onclose([](crow::websocket::connection& /*conn*/, const std::string& /*reason*/){
    // Connection closed; no explicit cleanup needed for per-request curl
  })
  .onmessage([api_key](crow::websocket::connection& conn, const std::string& data, bool is_binary){
  	
    if (is_binary) {
      try { conn.send_text(R"({"type":"error","error":"binary messages not supported"})"); } catch (...) {}
      return;
    }
    try {
      auto body = json::parse(data);
      std::string prompt = body.value("prompt", "");
      std::string model  = body.value("model", "mistralai/mistral-7b-instruct");
      std::cout << prompt << ", model: " << model;
      
      try {
      	std::string llm_key = "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"; 
	    std::string reply = Call(llm_key, prompt);
	    conn.send_text(reply.c_str()); 
	  	
		return;
	  	
	  } catch (...) {
	 		std::cout << "reply error.." << std::endl;	 
	  }
      
      bool stream = body.value("stream", true);
      if (prompt.empty()) {
        conn.send_text(R"({"type":"error","error":"missing 'prompt'"})");
        return;
      }
      if (!stream) {
        // Non-streaming over WS: single final response
        json payload = {
          {"model", model},
          {"messages", json::array({ json{{"role","user"},{"content",prompt}} })},
          {"stream", false}
        };
        try {
          std::string resp = http::post_json_blocking(OPENROUTER_URL, payload.dump(), api_key);
          auto j = json::parse(resp);
          std::string out;
          if (j.contains("choices") && !j["choices"].empty()) {
            out = j["choices"][0]["message"]["content"].get<std::string>();
          }
          json msg = {{"type","final"},{"text", out}};
          conn.send_text(msg.dump());
        } catch (const std::exception& e) {
          json err = {{"type","error"},{"error", e.what()}};
          try { conn.send_text(err.dump()); } catch (...) {}
        }
        return;
      }

      // Streaming path over WS
      CURL* curl = curl_easy_init();
      if (!curl) {
        conn.send_text(R"({"type":"error","error":"curl init failed"})");
        return;
      }

      json payload = {
        {"model", model},
        {"messages", json::array({ json{{"role","user"},{"content",prompt}} })},
        {"stream", true}
      };
      std::string body_str = payload.dump();

      struct curl_slist* headers = nullptr;
      headers = curl_slist_append(headers, "Content-Type: application/json");
      std::string auth = std::string("Authorization: Bearer ") + api_key;
      headers = curl_slist_append(headers, auth.c_str());
      headers = curl_slist_append(headers, "Accept: text/event-stream");
      headers = curl_slist_append(headers, "HTTP-Referer: http://localhost");
      headers = curl_slist_append(headers, "X-Title: QuarkSQL");

      struct WSState {
        crow::websocket::connection* pconn;
        std::string buffer;
      } st{&conn, ""};

      auto write_cb = +[](char* ptr, size_t size, size_t nmemb, void* userdata)->size_t {
        auto* s = static_cast<WSState*>(userdata);
        size_t bytes = size * nmemb;
        s->buffer.append(ptr, bytes);

        while (true) {
          size_t pos1 = s->buffer.find("\n\n");
          size_t pos2 = s->buffer.find("\r\n\r\n");
          size_t pos;
          size_t delimLen;
          if (pos1 == std::string::npos && pos2 == std::string::npos) break;
          if (pos2 == std::string::npos || (pos1 != std::string::npos && pos1 < pos2)) {
            pos = pos1; delimLen = 2;
          } else {
            pos = pos2; delimLen = 4;
          }

          std::string frame = s->buffer.substr(0, pos);
          s->buffer.erase(0, pos + delimLen);

          size_t start = 0;
          while (start < frame.size()) {
            size_t end = frame.find_first_of("\n\r", start);
            std::string line = (end == std::string::npos) ? frame.substr(start) : frame.substr(start, end - start);
            if (end == std::string::npos) start = frame.size(); else {
              if (frame[end] == '\r' && end + 1 < frame.size() && frame[end+1] == '\n') start = end + 2; else start = end + 1;
            }

            if (line.rfind("data:", 0) == 0) {
              std::string payload = line.substr(5);
              if (!payload.empty() && payload[0] == ' ') payload.erase(0,1);

              if (payload == "[DONE]") {
                try { s->pconn->send_text(R"({"type":"done"})"); } catch (...) {}
                continue;
              }
              try {
                auto j = json::parse(payload);
                std::string delta;
                if (j.contains("choices") && !j["choices"].empty() &&
                    j["choices"][0].contains("delta") &&
                    j["choices"][0]["delta"].contains("content") &&
                    !j["choices"][0]["delta"]["content"].is_null()) {
                  delta = j["choices"][0]["delta"]["content"].get<std::string>();
                }
                if (!delta.empty()) {
                  json msg = {{"type","delta"},{"delta",delta}};
                  try { s->pconn->send_text(msg.dump()); } catch (...) {}
                }
              } catch (...) {
                json msg = {{"type","delta"},{"delta",payload}};
                try { s->pconn->send_text(msg.dump()); } catch (...) {}
              }
            }
          }
        }
        return bytes;
      };

      curl_easy_setopt(curl, CURLOPT_URL, OPENROUTER_URL);
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &st);

      CURLcode code = curl_easy_perform(curl);
      if (code != CURLE_OK) {
        json err = {{"type","error"},{"error", std::string("curl: ")+curl_easy_strerror(code)}};
        try { conn.send_text(err.dump()); } catch (...) {}
      }

      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
    } catch (const std::exception& e) {
      json err = {{"type","error"},{"error", e.what()}};
      try { conn.send_text(err.dump()); } catch (...) {}
    }
  });
}

// ---------- Simple direct call for testing ----------
void Mistral::TestCall(const std::string& api_key) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    json payload = {
        {"model", "mistral-small-latest"},
        {"messages", json::array({
            json{{"role","user"}, {"content","Hello, Mistral! Can you tell me a joke?"}}
        })},
        {"stream", false}
    };
    std::string body_str = payload.dump();

    // Set up headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth = "Authorization: Bearer " + api_key;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "HTTP-Referer: http://localhost");
    headers = curl_slist_append(headers, "X-Title: QuarkSQL");

    // Capture output
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, OPENROUTER_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http::write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(res));
    }

    // Clean up curl
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // Parse JSON and print reply
    auto j = json::parse(response);
    if (j.contains("choices") && !j["choices"].empty()) {
        std::string out = j["choices"][0]["message"]["content"].get<std::string>();
        std::cout << "Mistral replied:\n" << out << "\n";
    } else {
        std::cerr << "Unexpected response format:\n" << response << "\n";
    }
}

std::string Mistral::Call(std::string api_key, std::string prompt){
	CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    json payload = {
        {"model", "mistralai/mistral-7b-instruct"},
        {"messages", json::array({
            json{{"role","user"}, {"content", prompt}}
        })},
        {"stream", false}
    };
    std::string body_str = payload.dump();

    // Set up headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth = "Authorization: Bearer " + api_key;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "HTTP-Referer: http://localhost");
    headers = curl_slist_append(headers, "X-Title: QuarkSQL");

    // Capture output
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, OPENROUTER_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http::write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(res));
    }
	// Parse JSON and print reply
    auto j = json::parse(response);
    
	std::string out;
    if (j.contains("choices") && !j["choices"].empty()) {
        out = j["choices"][0]["message"]["content"].get<std::string>();
        std::cout << "Mistral replied:\n" << out << "\n";
    } else {
        std::cerr << "Unexpected response format:\n" << response << "\n";
    }	
    
    // Clean up curl
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return out;
	
}
