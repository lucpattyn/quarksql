// File: JsonUtils.cpp
#include "JsonUtils.h"
#include <stdexcept>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <regex>

time_t parseDate(const std::string& s) {
    std::tm tm = {};
    if (strptime(s.c_str(), "%Y-%m-%d", &tm) == nullptr)
        throw std::runtime_error("Bad date: " + s);
    // treat as UTC midnight (UTC)
    return timegm(&tm);
}

std::regex likeToRegex(const std::string& pat) {
    std::string r = "^";
    for (char c : pat) {
        if (c == '%') {
            r += ".*";
        } else if (c == '_') {
            r += ".";
        } else if (std::ispunct(static_cast<unsigned char>(c))) {
            r += '\\';
            r += c;
        } else {
            r += c;
        }
    }
    r += "$";
    return std::regex(r, std::regex::icase);
}

bool evalPredicate(const std::string& fv,
                   const std::string& op,
                   const std::string& lit)
{
    static thread_local std::unordered_map<std::string,std::regex> regexCache;
    bool isDate = std::regex_match(lit, std::regex(R"(\d{4}-\d{2}-\d{2})"));
    bool isNum  = std::regex_match(lit, std::regex(R"(^-?\d+(\.\d+)?$)"));

    if (op == "=")  return fv == lit;
    if (op == "!=") return fv != lit;

    if (op == "<" || op == ">") {
        if (isNum) {
            double a = std::stod(fv), b = std::stod(lit);
            return (op=="<") ? a < b : a > b;
        }
        if (isDate) {
            return (op=="<")
              ? parseDate(fv) < parseDate(lit)
              : parseDate(fv) > parseDate(lit);
        }
        // lexicographic fallback
        return (op=="<") ? fv < lit : fv > lit;
    }

    if (op == "LIKE") {
        auto it = regexCache.find(lit);
        if (it == regexCache.end()) {
            regexCache.emplace(lit, likeToRegex(lit));
        }
        return std::regex_match(fv, regexCache[lit]);
    }

    throw std::runtime_error("Unknown op: " + op);
}

