// include/JsonUtils.h
#pragma once

#include <string>
#include <regex>
#include <ctime>
#include <map>

namespace JsonUtils{
	
	/**
	 * parseDate :: ISO8601 string (YYYY-MM-DD) ? time_t UTC
	 */
	time_t parseDate(const std::string& s);
	
	/**
	 * likeToRegex :: SQL LIKE pattern ('%foo_') ? std::regex
	 */
	std::regex likeToRegex(const std::string& pattern);
	
	/**
	 * evalPredicate :: compare a fieldValue against an operator and literal
	 */
	bool evalPredicate(const std::string& fieldValue,
	                   const std::string& op,
	                   const std::string& literal);
	
	/**
	 * parseToMap :: Parses a flat JSON object into a std::map<string,string>
	 *   - jsonStr must be like {"col1":"val","col2":123,...}
	 *   - Non-string values (numbers/booleans) are converted to their string form.
	 * @throws runtime_error on invalid JSON or non-object root
	 */
	std::map<std::string,std::string> parseToMap(const std::string& jsonStr);
	
}
