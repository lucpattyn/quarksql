// File: QueryExecutor.h
#pragma once
#include "SqlParser.h"
#include <crow.h>

// safe conversion of a crow::json::rvalue to string
static std::string rvalue_to_string(const crow::json::rvalue& v) {
    if (!v) return "";
    switch (v.t()) {
        case crow::json::type::String:
            return v.s();
        case crow::json::type::Number: {
            // Prefer integer representation if it's integral
            double d = v.d();
            int64_t i = v.i();
            if (static_cast<double>(i) == d) {
                return std::to_string(i);
            } else {
                std::ostringstream ss;
                ss << d;
                return ss.str();
            }
        }
        case crow::json::type::True:
            return "true";
        case crow::json::type::False:
            return "false";
            
        default:
            // fallback: serialize complex types
            return crow::json::dump(v);
    }
}

class QueryExecutor {
public:
    // Execute query AST and return JSON result array (with grouping/counts if specified)
    static crow::json::wvalue execute(const Query& q);
};

