#pragma once
#include <string>
#include <regex>
#include <ctime>

// parse ISO8601 date (YYYY-MM-DD) -> time_t UTC
time_t parseDate(const std::string& s);

// convert SQL-LIKE pattern ('%foo_') -> std::regex
std::regex likeToRegex(const std::string& pattern);

// evaluate a single field-value vs operator+literal
bool evalPredicate(const std::string& fieldValue,
                   const std::string& op,
                   const std::string& literal);
