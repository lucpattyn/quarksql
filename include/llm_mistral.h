#pragma once
#include <string>

#include "authmiddleware.h"

class Mistral{
	
public:
/**
 * @brief Wire Mistral chat routes (HTTP + WebSocket) into the Crow app.
 *
 * Routes:
 * - POST /ask          → non-streaming completion
 * - POST /ask/stream   → (legacy) SSE proxy streaming
 * - WS   /mistral/ws   → WebSocket streaming ({"type":"delta","delta":"..."})
 *
 * @param crowApp  Crow application instance (e.g., crow::App<AuthMiddleware>*)
 * @param api_key  API key string for the upstream API
 */
static void SetupRoutes(crow::App<AuthMiddleware>* crowApp, const std::string api_key);
static std::string Call(std::string api_key, std::string prompt);

static void TestCall(const std::string& api_key);
	

};

