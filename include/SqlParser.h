// File: SqlParser.h
#pragma once
#include <string>
#include <vector>

// Single predicate: field op value
struct Condition {
    std::string field, op, value;
};
// AND-group of predicates
using CondGroup = std::vector<Condition>;

// JOIN spec
struct JoinSpec {
    std::string leftTable, leftField;
    std::string rightTable, rightField;
};

// ORDER BY spec
struct OrderSpec {
    std::string field;
    bool        asc = true;
};

// AST for supported queries
struct Query {
    std::vector<std::string> selectCols;  // empty == *
    std::vector<std::string> tables;      // primary + any joined
    std::vector<JoinSpec>    joins;
    std::vector<CondGroup>   where;       // OR of AND-groups
    std::vector<std::string> groupBy;
    std::vector<OrderSpec>   orderBy;
};

class SqlParser {
public:
    // throws std::runtime_error on parse error
    static Query parse(const std::string& sql);
};

