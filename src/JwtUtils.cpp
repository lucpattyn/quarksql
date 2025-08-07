// src/JwtUtils.cpp
#include "JwtUtils.h"
#include <jwt-cpp/jwt.h>
#include <picojson/picojson.h>
#include <stdexcept>

std::string CppSignJwt(const std::string& payloadJson, const std::string& secret) {
    // Parse payloadJson into picojson::value
    picojson::value v;
    std::string err = picojson::parse(v, payloadJson);
    if (!err.empty()) {
        throw std::runtime_error("JSON parse error: " + err);
    }

    // Create and sign JWT with a "data" claim
    auto token = jwt::create()
        .set_payload_claim("data", jwt::claim(v))
        .sign(jwt::algorithm::hs256{secret});

    return token;
}

std::string CppVerifyJwt(const std::string& token, const std::string& secret) {
    // Decode and verify signature
    auto decoded = jwt::decode(token);
    auto verifier = jwt::verify()
        .allow_algorithm(jwt::algorithm::hs256{secret});
    verifier.verify(decoded);

    // Extract the "data" claim as picojson::value
    auto claim = decoded.get_payload_claim("data");
    picojson::value v = claim.to_json();  // this returns a picojson::value

    // Serialize to string (minified)
    return v.serialize();

}

