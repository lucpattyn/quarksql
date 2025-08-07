// src/JsonUtils.cpp
#include "JsonUtils.h"
#include <crow/json.h>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <cmath>

using crow::json::rvalue;
using crow::json::load;

// parseDate implementation
time_t parseDate(const std::string& s) {
    std::tm tm = {};
    if (strptime(s.c_str(), "%Y-%m-%d", &tm) == nullptr) {
        throw std::runtime_error("parseDate: invalid date format");
    }
    // Convert to time_t (UTC)
    #ifdef _WIN32
      return _mkgmtime(&tm);
    #else
      return timegm(&tm);
    #endif
}

// likeToRegex implementation
std::regex likeToRegex(const std::string& pattern) {
    std::string re = "^";
    for (char c : pattern) {
        if (c == '%')      re += ".*";
        else if (c == '_') re += ".";
        else if (std::isalnum(c)) re += c;
        else re += std::string("\\") + c;
    }
    re += "$";
    return std::regex(re);
}

// evalPredicate implementation
bool evalPredicate(const std::string& fieldValue,
                   const std::string& op,
                   const std::string& literal) {
    if (op == "=")  return fieldValue == literal;
    if (op == "!=") return fieldValue != literal;
    if (op == "<")  return fieldValue <  literal;
    if (op == "<=") return fieldValue <= literal;
    if (op == ">")  return fieldValue >  literal;
    if (op == ">=") return fieldValue >= literal;
    if (op == "LIKE") {
        static std::regex cache; // placeholder
        std::regex re = likeToRegex(literal);
        return std::regex_match(fieldValue, re);
    }
    throw std::runtime_error("evalPredicate: unknown operator " + op);
}

// parseToMap implementation
std::map<std::string,std::string> JsonUtils::parseToMap(const std::string& jsonStr) {
    rvalue j = load(jsonStr);
    if (!j || j.t() != crow::json::type::Object) {
        throw std::runtime_error("parseToMap: invalid JSON object");
    }

    std::map<std::string,std::string> out;
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& key = it->key();
        const rvalue& v = *it;
        switch (v.t()) {
          case crow::json::type::String:
            out[key] = v.s();
            break;
          case crow::json::type::Number: {
            // remove trailing zeros
            std::ostringstream os;
            os << std::fixed << std::setprecision(6) << v.d();
            std::string s = os.str();
            // strip unnecessary zeros and decimal point
            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
            if (!s.empty() && s.back()=='.') s.pop_back();
            out[key] = s;
            break;
          }
          case crow::json::type::True:
            out[key] = "true";
            break;
          case crow::json::type::False:
            out[key] = "false";
            break;
		  case crow::json::type::Null:
            out[key] = "null";
            break;
          default:
            // arrays/objects not flattened
            out[key] = "";
            break;
        }
    }
    return out;
}

