// include/JwtUtils.h
#pragma once
#include <string>

/**
 * Sign the given JSON payload (as a string) using the specified secret.
 * Returns a JWT string.
 * 
 * @param payloadJson  A JSON string representing the payload (e.g. "{\"user\":\"alice\"}")
 * @param secret       The HMAC secret key
 * @throws std::runtime_error on JSON parse or signing errors
 */
std::string CppSignJwt(const std::string& payloadJson, const std::string& secret);

/**
 * Verify the given JWT using the specified secret.
 * Returns the payload as a JSON string (minified).
 * 
 * @param token   The JWT to verify
 * @param secret  The HMAC secret key
 * @throws std::runtime_error on verification or JSON parse errors
 */
std::string CppVerifyJwt(const std::string& token, const std::string& secret);

