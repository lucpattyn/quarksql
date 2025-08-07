// include/SchemaManager.h
#pragma once
#include <string>
#include <unordered_map>
#include "crow/json.h"

struct TableSchema {
    // Map of field ? type (as string)
    std::unordered_map<std::string, std::string> indexedFields;
};

class SchemaManager {
public:
    /**
     * Load schemas from a JSON file at `path`.
     * Expects a top-level object where each key is a table name and its
     * value is an object with an "indexedFields" object inside.
     */
    static void loadFromFile(const std::string& path);

    /// Get the schema for a given table (throws if missing)
    static const TableSchema& getSchema(const std::string& table);
    
    static const std::unordered_map<std::string, TableSchema>& allSchemas() {
    	return schemas_;
	}

private:
    static std::unordered_map<std::string, TableSchema> schemas_;
};

