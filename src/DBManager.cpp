// DBManager.cpp
#include "DBManager.h"
#include <rocksdb/options.h>
#include <json.hpp>           // nlohmann::json
#include "JsonUtils.h"        // parseToMap()

#include "SchemaManager.h"

using json = nlohmann::json;

DBManager& DBManager::instance() {
    static DBManager mgr;
    return mgr;
}

bool DBManager::init(const std::string& path) {
    rocksdb::Options opts;
    opts.create_if_missing = true;
    opts.create_missing_column_families = true;

    // 1) discover existing CFs
    std::vector<std::string> cf_names;
    auto s = rocksdb::DB::ListColumnFamilies(opts, path, &cf_names);
    if (!s.ok()) cf_names.clear();

    // 2) add tables from schema
    for (auto& [tbl,_] : SchemaManager::allSchemas())
        cf_names.push_back(tbl);

    // 3) open DB with those CFs
    std::vector<rocksdb::ColumnFamilyDescriptor> descs;
    descs.reserve(cf_names.size());
    for (auto& name : cf_names)
        descs.emplace_back(name, rocksdb::ColumnFamilyOptions());

    std::vector<rocksdb::ColumnFamilyHandle*> handles;
    rocksdb::DB* raw = nullptr;
    s = rocksdb::DB::Open(opts, path, descs, &handles, &raw);
    if (!s.ok()) {
        std::cerr << "RocksDB open error: " << s.ToString() << "\n";
        return false;
    }
    _db.reset(raw);

    // 4) map names?handles
    for (size_t i = 0; i < cf_names.size(); ++i)
        _cfs[cf_names[i]] = handles[i];

    return true;
}

rocksdb::ColumnFamilyHandle* DBManager::cf(const std::string& name) {
    if (auto it = _cfs.find(name); it != _cfs.end())
        return it->second;
    rocksdb::ColumnFamilyHandle* h = nullptr;
    if (_db->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), name, &h).ok()) {
        _cfs[name] = h;
        return h;
    }
    return nullptr;
}

void DBManager::insert(const std::string &table,
                       const std::map<std::string,std::string> &row)
{
    // serialize to JSON
    json j(row);
    // Prefer explicit 'id' field as primary key if present
    std::string key;
    if (auto itId = row.find("id"); itId != row.end()) {
        key = itId->second;
    } else {
        // Fallback: first map key's value (legacy behavior)
        auto it = row.begin();
        key = (it == row.end()) ? std::string() : it->second;
    }
    _db->Put(rocksdb::WriteOptions(), cf(table), key, j.dump());
}

void DBManager::update(const std::string &table,
                       const std::string &key,
                       const std::map<std::string,std::string> &row)
{
    // merge into existing
    auto existing = get(table, key);
    for (auto &p : row) existing[p.first] = p.second;
    json j(existing);
    _db->Put(rocksdb::WriteOptions(), cf(table), key, j.dump());
}

void DBManager::remove(const std::string &table,
                       const std::string &key)
{
    _db->Delete(rocksdb::WriteOptions(), cf(table), key);
}

std::vector<std::string>
DBManager::scan(const std::string &table,
                const std::vector<Condition> &conds,
                int skip,
                int limit) const
{
    std::vector<std::string> keys;
    auto* handle = DBManager::instance().cf(table);
    auto it = std::unique_ptr<rocksdb::Iterator>(
        _db->NewIterator(rocksdb::ReadOptions(), handle)
    );

    int seen = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        auto k = it->key().ToString();
        auto row = get(table, k);

        bool ok = true;
        for (auto &c : conds) {
            auto &lhs = row[c.key];
            if (!( (c.op=="=")   ? lhs==c.value
                : (c.op=="!=")  ? lhs!=c.value
                : (c.op=="<")   ? lhs< c.value
                : (c.op==">")   ? lhs> c.value
                : (c.op=="<=")  ? lhs<=c.value
                : (c.op==">=")  ? lhs>=c.value
                : (c.op=="LIKE")? lhs.find(c.value)!=std::string::npos
                                : false ))
            {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        if (seen++ < skip) continue;
        keys.push_back(k);
        if (limit>0 && (int)keys.size() >= limit) break;
    }
    return keys;
}

std::map<std::string,std::string>
DBManager::get(const std::string &table,
               const std::string &key) const
{
    std::string val;
    _db->Get(rocksdb::ReadOptions(),  DBManager::instance().cf(table), key, &val);
    // use your JsonUtils to convert JSON?map<string,string>
    return JsonUtils::parseToMap(val);
}

