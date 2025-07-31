#include "IndexManager.h"
#include "DBManager.h"
#include "QueryExecutor.h"

#include <crow.h>
#include <algorithm>
#include <memory>


decltype(IndexManager::index) IndexManager::index;

void IndexManager::rebuildAll() {
    auto& mgr = DBManager::instance();
    for (auto& [table, cf] : mgr.all_cfs()) {
        std::unique_ptr<rocksdb::Iterator> it(
          mgr.db()->NewIterator(rocksdb::ReadOptions(), cf));
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            auto key = it->key().ToString();
            auto val = it->value().ToString();
            auto js  = crow::json::load(val);
            if (!js) continue;
            std::map<std::string,std::string> row;
            for (auto& p : js) row[p.key()] = rvalue_to_string(p);
            add(table, key, row);
        }
    }
}

void IndexManager::add(const std::string& table,
                       const std::string& key,
                       const std::map<std::string,std::string>& row)
{
    remove(table, key);
    for (auto& [f,v] : row)
        index[table][f][v].push_back(key);
}

void IndexManager::remove(const std::string& table,
                          const std::string& key)
{
    auto& fmap = index[table];
    for (auto& [field,m] : fmap) {
        for (auto& [val,keys] : m) {
            keys.erase(std::remove(keys.begin(),keys.end(),key),
                       keys.end());
        }
    }
}
