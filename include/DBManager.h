#pragma once
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <memory>
#include <map>
#include <mutex>

using namespace rocksdb;

class DBManager {
public:
    static DBManager& instance();
    bool init(const std::string& path);

    DB* db() { return _db.get(); }
    ColumnFamilyHandle* cf(const std::string& name);
    const std::map<std::string,ColumnFamilyHandle*>& all_cfs() const { return _cfs; }
    std::mutex& mutex() { return _mutex; }

private:
    DBManager() = default;
    std::unique_ptr<DB> _db;
    std::map<std::string,ColumnFamilyHandle*> _cfs;
    std::mutex _mutex;
};
