// DBManager.h
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <rocksdb/db.h>
#include "Query.h"

class DBManager {
public:
    // singleton
    static DBManager& instance();

    // open DB at path, loading/creating CFs for all schemas
    bool init(const std::string& path);

    // get or create column family
    rocksdb::ColumnFamilyHandle* cf(const std::string& name);

    // Basic CRUD used by QueryExecutor
    void insert(const std::string &table,
                const std::map<std::string,std::string> &row);
    void update(const std::string &table,
                const std::string &key,
                const std::map<std::string,std::string> &row);
    void remove(const std::string &table,
                const std::string &key);

    // scan + push-down of WHERE + SKIP/LIMIT ? returns keys only
    std::vector<std::string>
      scan(const std::string &table,
           const std::vector<Condition> &conds,
           int skip  = 0,
           int limit = -1) const;

    // fetch & parse one row
    std::map<std::string,std::string>
      get(const std::string &table,
          const std::string &key) const;

    // expose for IndexManager
    rocksdb::DB* db() const { return _db.get(); }
    const std::map<std::string,rocksdb::ColumnFamilyHandle*>&
      all_cfs() const { return _cfs; }

private:
    DBManager() = default;
    std::unique_ptr<rocksdb::DB>                       _db;
    std::map<std::string, rocksdb::ColumnFamilyHandle*> _cfs;
};

