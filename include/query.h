// Query.h
#ifndef QUERY_H
#define QUERY_H

#include <string>
#include <vector>
#include <map>
#include <iostream>

// Supported query types
enum class QueryType { INSERT, UPDATE, DELETE, BATCH, SELECT };
inline const char* qt_string(QueryType t) {
    switch (t) {
        case QueryType::INSERT: return "INSERT";
        case QueryType::UPDATE: return "UPDATE";
        case QueryType::DELETE: return "DELETE";
        case QueryType::BATCH:  return "BATCH";
        case QueryType::SELECT: return "SELECT";
    }
    return "UNKNOWN";
}

// One condition in a WHERE clause
struct Condition {
    std::string key;     // column or qualified column (table.column)
    std::string op;      // =, !=, <, >, <=, >=, LIKE
    std::string value;   // right-hand-side value
};

// One JOIN clause
struct Join {
    std::string leftTable;
    std::string leftField;
    std::string rightTable;
    std::string rightField;
};

// Parsed query representation
struct Query {
    QueryType type = QueryType::SELECT;
    std::string table;                   // main table

    // For INSERT/UPDATE
    std::map<std::string, std::string> rowData;
    // For BATCH
    std::vector<std::map<std::string, std::string>> batchData;
    // For DELETE … KEYS [...]
    std::vector<std::string> deleteKeys;

    // For SELECT
    std::vector<std::string> selectCols;
    bool isCount = false;
    std::string groupBy;                 // e.g. "user"
    std::string orderByField;            // e.g. "stock"
    bool orderDesc = false;              // true if DESC

    // Pagination
    int skip  = 0;
    int limit = -1;                      // -1 = no limit

    // Common
    std::vector<Condition> conditions;
    std::vector<Join>      joins;
    
    void print() const {
	    std::cout << "Query {\n";
	    std::cout << "  type        = " << qt_string(type) << "\n";
	    std::cout << "  table       = " << table << "\n";
	
	    // INSERT/UPDATE
	    if (!rowData.empty()) {
	        std::cout << "  rowData     = {\n";
	        for (auto &p : rowData)
	            std::cout << "    " << p.first << " = " << p.second << "\n";
	        std::cout << "  }\n";
	    }
	
	    // BATCH
	    if (!batchData.empty()) {
	        std::cout << "  batchData   = [\n";
	        for (size_t i = 0; i < batchData.size(); ++i) {
	            std::cout << "    row " << i << " {\n";
	            for (auto &p : batchData[i])
	                std::cout << "      " << p.first << " = " << p.second << "\n";
	            std::cout << "    }\n";
	        }
	        std::cout << "  ]\n";
	    }
	
	    // DELETE KEYS
	    if (!deleteKeys.empty()) {
	        std::cout << "  deleteKeys  = [ ";
	        for (auto &k : deleteKeys)
	            std::cout << k << " ";
	        std::cout << "]\n";
	    }
	
	    // SELECT-specific
	    if (!selectCols.empty()) {
	        std::cout << "  selectCols  = [ ";
	        for (auto &c : selectCols)
	            std::cout << c << " ";
	        std::cout << "]\n";
	    }
	    std::cout << "  isCount     = " << (isCount ? "true" : "false") << "\n";
	    if (!groupBy.empty())
	        std::cout << "  groupBy     = " << groupBy << "\n";
	    if (!orderByField.empty())
	        std::cout << "  orderBy     = " << orderByField
	                  << (orderDesc ? " DESC" : " ASC") << "\n";
	
	    // Pagination
	    std::cout << "  skip        = " << skip
	              << ", limit = " << limit << "\n";
	
	    // WHERE conditions
	    if (!conditions.empty()) {
	        std::cout << "  conditions  = [\n";
	        for (auto &c : conditions)
	            std::cout << "    " << c.key << " " << c.op
	                      << " '" << c.value << "'\n";
	        std::cout << "  ]\n";
	    }
	
	    // JOIN clauses
	    if (!joins.empty()) {
	        std::cout << "  joins       = [\n";
	        for (auto &j : joins)
	            std::cout << "    " << j.leftTable << "." << j.leftField
	                      << " = " << j.rightTable << "." << j.rightField << "\n";
	        std::cout << "  ]\n";
	    }
	
	    std::cout << "}\n";
	}

};

// One row of result: column?value
struct QueryResultRow {
    std::map<std::string, std::string> vals;
};

// Overall outcome
struct QueryResult {
    std::vector<QueryResultRow> rows;
    int affected = 0;  // # of rows returned or modified
};

#endif // QUERY_H

