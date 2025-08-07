// SqlParser.h
#ifndef SQLPARSER_H
#define SQLPARSER_H

#include <string>
#include "Query.h"

class SqlParser {
public:
    // Parse raw SQL into our Query struct
    static Query parse(const std::string &sql);
};

#endif // SQLPARSER_H

