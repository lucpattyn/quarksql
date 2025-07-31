#include "DBManager.h"
#include <iostream>

DBManager& DBManager::instance() {
    static DBManager mgr;
    return mgr;
}

bool DBManager::init(const std::string& path) {
    DBOptions opts;
    opts.create_if_missing = true;
    opts.create_missing_column_families = true;

    std::vector<std::string> cf_names;
    Status s = DB::ListColumnFamilies(opts, path, &cf_names);
    if (!s.ok()) cf_names = {"default"};

    std::vector<ColumnFamilyDescriptor> descs;
    for (auto& name : cf_names)
        descs.emplace_back(name, ColumnFamilyOptions());

    std::vector<ColumnFamilyHandle*> handles;
    DB* raw = nullptr;
    s = DB::Open(opts, path, descs, &handles, &raw);
    if (!s.ok()) {
        std::cerr << "RocksDB open error: " << s.ToString() << "\n";
        return false;
    }
    _db.reset(raw);
    for (size_t i = 0; i < cf_names.size(); ++i)
        _cfs[cf_names[i]] = handles[i];
    return true;
}

ColumnFamilyHandle* DBManager::cf(const std::string& name) {
    auto it = _cfs.find(name);
    if (it != _cfs.end()) return it->second;

    ColumnFamilyHandle* h = nullptr;
    if (_db->CreateColumnFamily(ColumnFamilyOptions(), name, &h).ok()) {
        _cfs[name] = h;
        return h;
    }
    return nullptr;
}
