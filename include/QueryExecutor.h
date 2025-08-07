// QueryExecutor.h
#ifndef QUERYEXECUTOR_H
#define QUERYEXECUTOR_H

#include "Query.h"

class QueryExecutor {
public:
    static void execute(const Query &q, QueryResult &r);
    static void execute(std::string sql, QueryResult& r);
private:
    static void handleInsert(const Query&, QueryResult&);
    static void handleUpdate(const Query&, QueryResult&);
    static void handleDelete(const Query&, QueryResult&);
    static void handleBatch (const Query&, QueryResult&);
    static void handleSelect(const Query&, QueryResult&);
};

#endif // QUERYEXECUTOR_H

