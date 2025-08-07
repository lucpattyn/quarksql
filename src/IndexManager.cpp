// IndexManager.cpp
#include "IndexManager.h"
#include "SchemaManager.h"
#include "DBManager.h"
#include "JsonUtils.h"         // parseToMap()
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <iostream>

// Define the static member
std::unordered_map<
    std::string,
    std::unordered_map<std::string, std::multimap<std::string, std::string>>
> IndexManager::index;

// Check whether we have an in-memory index built for table.field
bool IndexManager::hasIndex(const std::string &table,
                            const std::string &field)
{
    auto ti = index.find(table);
    if (ti == index.end()) return false;
    auto &fieldMap = ti->second;
    return fieldMap.find(field) != fieldMap.end();
}


void IndexManager::rebuildAll() {
    index.clear();
    auto& mgr = DBManager::instance();

    // Only rebuild indices for tables defined in your schemas
    for (auto& [table, schema] : SchemaManager::allSchemas()) {
        std::cout << "[IndexManager] Rebuilding index for table: " << table << std::endl;

        // Get-or-create the column-family handle for this table
        auto* cf = mgr.cf(table);
        if (!cf) {
            std::cerr << "[IndexManager] Warning: no column family for table " << table << std::endl;
            continue;
        }

        // Iterate all rows in this CF
        std::unique_ptr<rocksdb::Iterator> it(
            mgr.db()->NewIterator(rocksdb::ReadOptions(), cf)
        );
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            const std::string key   = it->key().ToString();
            const std::string value = it->value().ToString();

            // Parse JSON into a map<string,string>
            std::map<std::string, std::string> row;
            try {
                row = JsonUtils::parseToMap(value);
            } catch (const std::exception& e) {
                std::cerr << "[IndexManager] Skipping invalid JSON in '"
                          << table << "' (key=" << key << "): "
                          << e.what() << std::endl;
                continue;
            }

            // For each indexed field in the schema, insert into the multimap
            for (auto& field_pair : schema.indexedFields) {
                const std::string& field = field_pair.first;
                auto itr = row.find(field);
                if (itr != row.end()) {
                    index[table][field].insert({ itr->second, key });
                }
            }
        }
    }
}

void IndexManager::add(
    const std::string& table,
    const std::string& key,
    const std::map<std::string, std::string>& row,
    const std::map<std::string, std::string>& oldRow
) {
    const auto& schema = SchemaManager::getSchema(table);

    for (auto& field_pair : schema.indexedFields) {
        const std::string& field = field_pair.first;
        const std::string oldVal = oldRow.count(field) ? oldRow.at(field) : "";
        const std::string newVal = row   .count(field) ? row   .at(field) : "";

        // Remove stale entries
        if (!oldVal.empty() && oldVal != newVal) {
            auto& mmap = index[table][field];
            auto range = mmap.equal_range(oldVal);
            for (auto it = range.first; it != range.second; /* no increment here */) {
                if (it->second == key) it = mmap.erase(it);
                else ++it;
            }
        }
        // Insert new entries
        if (!newVal.empty() && newVal != oldVal) {
            index[table][field].insert({ newVal, key });
        }
    }
}

void IndexManager::remove(
    const std::string& table,
    const std::string& key,
    const std::map<std::string, std::string>& oldRow
) {
    const auto& schema = SchemaManager::getSchema(table);

    for (auto& field_pair : schema.indexedFields) {
        const std::string& field = field_pair.first;
        auto itOld = oldRow.find(field);
        if (itOld == oldRow.end()) continue;
        const std::string& oldVal = itOld->second;

        auto& mmap = index[table][field];
        auto range = mmap.equal_range(oldVal);
        for (auto it = range.first; it != range.second; /* no increment here */) {
            if (it->second == key) it = mmap.erase(it);
            else ++it;
        }
    }
}

