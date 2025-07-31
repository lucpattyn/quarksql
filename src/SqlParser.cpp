// File: SqlParser.cpp
#include "SqlParser.h"
#include <regex>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <cctype>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string uppercase(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return std::toupper(c); });
    return r;
}

Query SqlParser::parse(const std::string& raw_sql) {
    std::string sql = trim(raw_sql);
    if (!sql.empty() && sql.back() == ';') sql.pop_back(); // strip trailing ;
    
    // CASE-INSENSITIVE work: we'll use regex with icase for keywords
    std::smatch m;

    Query q;

    // 1. SELECT ... FROM ...
    std::regex select_from_re(R"(^\s*SELECT\s+(.+?)\s+FROM\s+(\w+))", std::regex::icase);
    if (!std::regex_search(sql, m, select_from_re))
        throw std::runtime_error("Could not parse SELECT/FROM");
    std::string cols = trim(m[1].str());
    std::string primary = m[2].str();
    if (cols != "*") {
        std::stringstream ss(cols);
        std::string c;
        while (std::getline(ss, c, ',')) {
            c.erase(std::remove_if(c.begin(), c.end(), ::isspace), c.end());
            if (!c.empty()) q.selectCols.push_back(c);
        }
    }
    q.tables.push_back(primary);

    // Remove parsed prefix so we can consume rest
    size_t pos = m.position(0) + m.length(0);
    std::string rest = sql.substr(pos);

    // 2. Optional JOIN ... ON ...
    std::regex join_re(R"(\s+JOIN\s+(\w+)\s+ON\s+(\w+)\.(\w+)\s*=\s*(\w+)\.(\w+))", std::regex::icase);
    if (std::regex_search(rest, m, join_re)) {
        std::string rightTable = m[1].str();
        std::string leftTbl = m[2].str();
        std::string leftField = m[3].str();
        std::string rightTbl2 = m[4].str();
        std::string rightField = m[5].str();
        q.tables.push_back(rightTable);
        q.joins.push_back({
            leftTbl, leftField,
            rightTable, rightField
        });
        // cut out the matched join part
        rest = rest.substr(m.position(0) + m.length(0));
    }

    // 3. WHERE clause
    std::regex where_re(R"(\s+WHERE\s+(.+?)(?:\s+GROUP\s+BY|\s+ORDER\s+BY|$))", std::regex::icase);
    if (std::regex_search(rest, m, where_re)) {
        std::string where = trim(m[1].str());
        // Split on OR, but respect that AND has higher precedence: naive OR of AND groups
        std::regex or_re(R"(\s+OR\s+)", std::regex::icase);
        std::sregex_token_iterator or_it(where.begin(), where.end(), or_re, -1), or_end;
        for (; or_it != or_end; ++or_it) {
            CondGroup grp;
            std::string part = *or_it;
            std::regex and_re(R"(\s+AND\s+)", std::regex::icase);
            std::sregex_token_iterator and_it(part.begin(), part.end(), and_re, -1), and_end;
            for (; and_it != and_end; ++and_it) {
                std::string token = trim(*and_it);
                std::smatch c;
                static const std::regex cond_re(R"((\w+)\s*(=|!=|<|>|LIKE)\s*'([^']+)')", std::regex::icase);
                if (!std::regex_search(token, c, cond_re))
                    throw std::runtime_error("Bad condition: " + token);
                grp.push_back({c[1].str(), c[2].str(), c[3].str()});
            }
            q.where.push_back(std::move(grp));
        }
        // cut off where part for further parsing
        rest = rest.substr(m.position(0) + m.length(0));
    }

    // 4. GROUP BY
    std::regex group_re(R"(\s+GROUP\s+BY\s+(.+?)(?:\s+ORDER\s+BY|$))", std::regex::icase);
    if (std::regex_search(rest, m, group_re)) {
        std::string gb = trim(m[1].str());
        std::stringstream ss(gb);
        std::string g;
        while (std::getline(ss, g, ',')) {
            g.erase(std::remove_if(g.begin(), g.end(), ::isspace), g.end());
            if (!g.empty()) q.groupBy.push_back(g);
        }
        rest = rest.substr(m.position(0) + m.length(0));
    }

    // 5. ORDER BY
    std::regex order_re(R"(\s+ORDER\s+BY\s+(.+)$)", std::regex::icase);
    if (std::regex_search(rest, m, order_re)) {
        std::string ob = trim(m[1].str());
        std::stringstream ss(ob);
        std::string o;
        while (std::getline(ss, o, ',')) {
            bool asc = true;
            if (std::regex_search(o, std::regex(R"(\s+DESC$)", std::regex::icase))) asc = false;
            o = std::regex_replace(o, std::regex(R"(\s+ASC$|\s+DESC$)", std::regex::icase), "");
            o.erase(0, o.find_first_not_of(" \t"));
            q.orderBy.push_back({o, asc});
        }
    }

    return q;
}

