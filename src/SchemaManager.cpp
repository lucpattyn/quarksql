// src/SchemaManager.cpp
#include "SchemaManager.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

using crow::json::rvalue;
using crow::json::load;

std::unordered_map<std::string, TableSchema> SchemaManager::schemas_;

void SchemaManager::loadFromFile(const std::string& path) {
    // 1) Read entire file into a string
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open schema file: " + path);
    std::ostringstream buf;
    buf << in.rdbuf();

    // 2) Parse with Crow
    rvalue root = load(buf.str());
    if (!root) throw std::runtime_error("Invalid JSON in schema file");

    // 3) Iterate each table entry
    for (auto it = root.begin(); it != root.end(); ++it) {
        const std::string tableName = it->key();
        const rvalue& tblObj = *it;
        if (!tblObj.has("indexedFields")) continue;

        const rvalue& idx = tblObj["indexedFields"];
        if (idx.t() != crow::json::type::Object) continue;

        TableSchema ts;
        // 4) Iterate each field?type pair
        for (auto fit = idx.begin(); fit != idx.end(); ++fit) {
            const std::string fieldName = fit->key();
            const rvalue& typeVal = *fit;
            if (typeVal.t() != crow::json::type::String) {
                throw std::runtime_error(
                  "Schema error: indexedFields." + fieldName + " must be a string");
            }
            // r_string has operator std::string()
            ts.indexedFields[fieldName] = std::string(typeVal.s());
        }

        schemas_.emplace(tableName, std::move(ts));
    }
}

const TableSchema& SchemaManager::getSchema(const std::string& table) {
	auto it = schemas_.find(table);
    if (it == schemas_.end()) {
        throw std::runtime_error("Schema not defined for table: " + table);
    }
    auto& t = it->second;
    /*for (const auto& [field, type] : t.indexedFields) {
    	std::cout << "Field: " << field << ", Type: " << type << std::endl;
	}*/	

    
    return it->second;
}

