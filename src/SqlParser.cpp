// SqlParser.cpp
#include "SqlParser.h"
#include <regex>
#include <sstream>
#include "json.hpp"            // nlohmann::json
using json = nlohmann::json;

// Patterns
static const std::regex insert_json_re(
  R"(^INSERT\s+INTO\s+(\w+)\s+VALUES\s*(\{.+\})$)", std::regex::icase);
static const std::regex insert_re(
  R"(^INSERT\s+INTO\s+(\w+)\s*\(([^)]+)\)\s*VALUES\s*\(([^)]+)\)$)",
  std::regex::icase);
static const std::regex update_json_re(
  R"(^UPDATE\s+(\w+)\s+SET\s*(\{.+\})(?:\s+WHERE\s+(.+))?$)",
  std::regex::icase);
static const std::regex update_re(
  R"(^UPDATE\s+(\w+)\s+SET\s*([^ ]+)\s*=\s*'([^']+)'(?:\s+WHERE\s+(.+))?$)",
  std::regex::icase);
static const std::regex delete_keys_re(
  R"(^DELETE\s+FROM\s+(\w+)\s+KEYS\s*(\[[^\]]+\])$)", std::regex::icase);
static const std::regex delete_re(
  R"(^DELETE\s+FROM\s+(\w+)(?:\s+WHERE\s+(.+))?$)", std::regex::icase);
static const std::regex batch_re(
  R"(^BATCH\s+(\w+)\s*(\{.+\})$)", std::regex::icase);
static const std::regex select_re(
  R"(^SELECT\s+(.+?)\s+FROM\s+(\w+))", std::regex::icase);
static const std::regex cond_re(
  R"((\w+(?:\.\w+)?)\s*(=|!=|<=|>=|<|>|LIKE)\s*'([^']+)')",
  std::regex::icase);
static const std::regex join_re(
  R"(\s+JOIN\s+(\w+)\s+ON\s+(\w+)\.(\w+)\s*=\s*(\w+)\.(\w+))",
  std::regex::icase);
static const std::regex group_re(
  R"(\s+GROUP\s+BY\s+(\w+(?:\.\w+)?))", std::regex::icase);
static const std::regex order_re(
  R"(\s+ORDER\s+BY\s+(\w+(?:\.\w+)?)(?:\s+(ASC|DESC))?)",
  std::regex::icase);
static const std::regex skip_re(
  R"(\s+SKIP\s+(\d+))", std::regex::icase);
static const std::regex limit_re(
  R"(\s+LIMIT\s+(\d+))", std::regex::icase);

// Helpers
static inline std::vector<std::string> split(const std::string &s, char d) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string itm;
    while (std::getline(ss, itm, d)) v.push_back(itm);
    return v;
}
static inline std::string trim(const std::string &s) {
    auto a = s.find_first_not_of(" \t\r\n");
    auto b = s.find_last_not_of (" \t\r\n");
    return (a==std::string::npos) ? "" : s.substr(a, b-a+1);
}

Query SqlParser::parse(const std::string &sql) {
    std::string s = trim(sql);
    // 2) If the last non-whitespace character is a semicolon, drop it
    if (!s.empty() && s.back() == ';') {
        s.pop_back();           // remove the ';'
        s = trim(s);            // re-trim in case there’s trailing space before it
    }
    
	std::smatch m;
    Query q;

    // INSERT JSON
    if (std::regex_match(s, m, insert_json_re)) {
        q.type  = QueryType::INSERT;
        q.table = m[1];
        std::string p = m[2].str();
        auto obj = json::parse(p);
        for (auto &it : obj.items()) {
            auto v = it.value();
            q.rowData[it.key()] = v.is_string()
                ? v.get<std::string>()
                : v.dump();
        }
        return q;
    }
    // INSERT (...) VALUES (...)
    if (std::regex_match(s, m, insert_re)) {
        q.type  = QueryType::INSERT;
        q.table = m[1];
        auto cols = split(m[2], ','), vals = split(m[3], ',');
        for (size_t i = 0; i < cols.size(); ++i)
            q.rowData[ trim(cols[i]) ] = trim(vals[i]);
        return q;
    }
    // UPDATE JSON [WHERE]
    if (std::regex_match(s, m, update_json_re)) {
        q.type  = QueryType::UPDATE;
        q.table = m[1];
        std::string p = m[2].str();
        auto obj = json::parse(p);
        for (auto &it : obj.items())
            q.rowData[it.key()] = it.value().is_string()
                ? it.value().get<std::string>()
                : it.value().dump();
        if (m.size()>3 && m[3].matched) {
            std::smatch cm;
            std::string whereClause = m[3].str();
            if (std::regex_search(whereClause, cm, cond_re))
                q.conditions.push_back(
                  { trim(cm[1]), cm[2], cm[3] }
                );
        }
        return q;
    }
    // UPDATE col='val' [WHERE]
    if (std::regex_match(s, m, update_re)) {
        q.type  = QueryType::UPDATE;
        q.table = m[1];
        q.rowData[ trim(m[2]) ] = m[3];
        if (m.size()>4 && m[4].matched) {
            std::smatch cm;
            std::string whereClause = m[4].str();
            if (std::regex_search(whereClause, cm, cond_re))
                q.conditions.push_back(
                  { trim(cm[1]), cm[2], cm[3] }
                );
        }
        return q;
    }
    // DELETE ... KEYS [...]
    if (std::regex_match(s, m, delete_keys_re)) {
        q.type  = QueryType::DELETE;
        q.table = m[1];
        std::string p = m[2].str();
        auto arr = json::parse(p);
        for (auto &v : arr) q.deleteKeys.push_back(v.get<std::string>());
        return q;
    }
    // DELETE FROM ... [WHERE]
    if (std::regex_match(s, m, delete_re)) {
        q.type  = QueryType::DELETE;
        q.table = m[1];
        if (m.size()>2 && m[2].matched) {
            std::smatch cm;
            std::string whereClause = m[2].str();
            if (std::regex_search(whereClause, cm, cond_re))
                q.conditions.push_back(
                  { trim(cm[1]), cm[2], cm[3] }
                );
        }
        return q;
    }
    // BATCH table { ... }
    if (std::regex_match(s, m, batch_re)) {
        q.type  = QueryType::BATCH;
        q.table = m[1];
        std::string p = m[2].str();
        auto obj = json::parse(p);
        for (auto &it : obj.items()) {
            std::map<std::string,std::string> row;
            for (auto &f : it.value().items())
                row[f.key()] = f.value().is_string()
                              ? f.value().get<std::string>()
                              : f.value().dump();
            q.batchData.push_back(row);
        }
        return q;
    }
    // SELECT ... FROM table ...
    if (std::regex_search(s, m, select_re)) {
        q.type = QueryType::SELECT;
        // columns
        for (auto &c : split(m[1], ','))
            q.selectCols.push_back(trim(c));
        if (q.selectCols.size()==1 &&
            std::regex_match(q.selectCols[0],
              std::regex(R"(^COUNT\(\*\)$)",std::regex::icase)))
        {
            q.isCount = true;
        }
        // table
        q.table = m[2];
        // JOINs
        for (auto it = std::sregex_iterator(s.begin(), s.end(), join_re);
             it != std::sregex_iterator(); ++it)
        {
            auto &jm = *it;
            q.joins.push_back({
              jm[2], jm[3], jm[4], jm[5]
            });
        }
        // WHEREs
        for (auto wit = std::sregex_iterator(s.begin(), s.end(), cond_re);
             wit!=std::sregex_iterator(); ++wit)
        {
            auto &cm = *wit;
            q.conditions.push_back({
              trim(cm[1]), cm[2], cm[3]
            });
        }
        // GROUP BY
        if (std::regex_search(s, m, group_re))
            q.groupBy = trim(m[1]);
        // ORDER BY
        if (std::regex_search(s, m, order_re)) {
            q.orderByField = trim(m[1]);
            if (m.size()>2 && m[2].matched &&
                strcasecmp(m[2].str().c_str(),"DESC")==0)
                q.orderDesc = true;
        }
        // SKIP/LIMIT
        if (std::regex_search(s, m, skip_re))
            q.skip = std::stoi(m[1]);
        if (std::regex_search(s, m, limit_re))
            q.limit = std::stoi(m[1]);
        return q;
    }

    throw std::runtime_error("Unsupported SQL: "+sql);
}

