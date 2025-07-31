// File: QueryExecutor.cpp
#include "QueryExecutor.h"
#include "DBManager.h"
#include "IndexManager.h"
#include "JsonUtils.h"

#include <set>
#include <map>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <regex>

using namespace std;

crow::json::wvalue QueryExecutor::execute(const Query& q) {
    auto& mgr = DBManager::instance();

    // 1. Distribute WHERE conditions per table
    map<string, vector<CondGroup>> perTable;
    for (const auto& grp : q.where) {
        map<string, CondGroup> tblGroup;
        for (const auto& cond : grp) {
            string target;
            for (const auto& t : q.tables) {
                if (IndexManager::index.count(t) && IndexManager::index[t].count(cond.field)) {
                    if (!target.empty())
                        throw runtime_error("Ambiguous field in WHERE: " + cond.field);
                    target = t;
                }
            }
            if (target.empty() && q.tables.size() == 1)
                target = q.tables[0];
            if (target.empty())
                throw runtime_error("Unknown field in WHERE: " + cond.field);
            tblGroup[target].push_back(cond);
        }
        for (auto& [tbl, cg] : tblGroup)
            perTable[tbl].push_back(cg);
    }

    // 2. Candidate key sets per table
    map<string, set<string>> tableKeys;
    for (const auto& tbl : q.tables) {
        set<string> candidate;
        if (!perTable.count(tbl)) {
            auto cf = mgr.cf(tbl);
            if (!cf) throw runtime_error("No table: " + tbl);
            unique_ptr<rocksdb::Iterator> it(
                mgr.db()->NewIterator(rocksdb::ReadOptions(), cf));
            for (it->SeekToFirst(); it->Valid(); it->Next())
                candidate.insert(it->key().ToString());
            tableKeys[tbl] = move(candidate);
            continue;
        }
        set<string> orUnion;
        for (const auto& grp : perTable[tbl]) {
            set<string> andSet;
            bool first = true;
            for (const auto& cond : grp) {
                set<string> thisKeys;
                if (cond.op == "=") {
                    for (const auto& k : IndexManager::index[tbl][cond.field][cond.value])
                        thisKeys.insert(k);
                } else if (cond.op == "!=") {
                    for (const auto& [val, vec] : IndexManager::index[tbl][cond.field]) {
                        if (val != cond.value)
                            thisKeys.insert(vec.begin(), vec.end());
                    }
                } else {
                    for (const auto& [val, vec] : IndexManager::index[tbl][cond.field]) {
                        if (evalPredicate(val, cond.op, cond.value))
                            thisKeys.insert(vec.begin(), vec.end());
                    }
                }
                if (first) {
                    andSet = thisKeys;
                    first = false;
                } else {
                    set<string> tmp;
                    set_intersection(andSet.begin(), andSet.end(),
                                     thisKeys.begin(), thisKeys.end(),
                                     inserter(tmp, tmp.begin()));
                    andSet = move(tmp);
                }
            }
            orUnion.insert(andSet.begin(), andSet.end());
        }
        tableKeys[tbl] = move(orUnion);
    }

    // 3. Row materialization
    struct Row { map<string, string> vals; };
    vector<Row> rows;

    if (q.joins.empty()) {
        const string& tbl = q.tables[0];
        for (const auto& key : tableKeys[tbl]) {
            string raw;
            if (mgr.db()->Get(rocksdb::ReadOptions(), mgr.cf(tbl), key, &raw).ok()) {
                auto js = crow::json::load(raw);
                if (!js) continue;
                Row r;
                for (auto& p : js) r.vals[p.key()] = rvalue_to_string(p);
                rows.push_back(move(r));
            }
        }
    } else {
        const auto& j = q.joins[0];
        const auto& leftTbl = j.leftTable;
        const auto& rightTbl = j.rightTable;
        const auto& leftField = j.leftField;
        const auto& rightField = j.rightField;

        for (const auto& lk : tableKeys[leftTbl]) {
            string lraw;
            if (!mgr.db()->Get(rocksdb::ReadOptions(), mgr.cf(leftTbl), lk, &lraw).ok()) continue;
            auto ljs = crow::json::load(lraw);
            if (!ljs) continue;
            //string lval = ljs[leftField].s();
            string lval = rvalue_to_string(ljs[leftField]);
			auto it_right_keys = IndexManager::index[rightTbl][rightField].find(lval);
            if (it_right_keys == IndexManager::index[rightTbl][rightField].end()) continue;
            for (const auto& rk : it_right_keys->second) {
                if (!tableKeys[rightTbl].count(rk)) continue;
                string rraw;
                if (!mgr.db()->Get(rocksdb::ReadOptions(), mgr.cf(rightTbl), rk, &rraw).ok()) continue;
                auto rjs = crow::json::load(rraw);
                if (!rjs) continue;
                Row r;
                for (auto& p : ljs) r.vals[leftTbl + "." + std::string(p.key())] = rvalue_to_string(p);
                for (auto& p : rjs) r.vals[rightTbl + "." + std::string(p.key())] = rvalue_to_string(p);
                rows.push_back(move(r));
            }
        }
    }

    // 4. GROUP BY (COUNT)
    if (!q.groupBy.empty()) {
        map<vector<string>, int> counts;
        for (const auto& row : rows) {
            vector<string> keyVec;
            for (const auto& fld : q.groupBy) {
                auto it = row.vals.find(fld);
                keyVec.push_back(it != row.vals.end() ? it->second : string());
            }
            counts[keyVec]++;
        }
        vector<Row> agg;
        for (const auto& [kv, cnt] : counts) {
            Row r;
            for (size_t i = 0; i < q.groupBy.size(); ++i)
                r.vals[q.groupBy[i]] = kv[i];
            r.vals["count"] = to_string(cnt);
            agg.push_back(r);
        }
        rows = move(agg);
    }

    // 5. ORDER BY
    if (!q.orderBy.empty()) {
        sort(rows.begin(), rows.end(), [&](const Row& A, const Row& B) {
            for (const auto& os : q.orderBy) {
                const string& f = os.field;
                const string& av = A.vals.count(f) ? A.vals.at(f) : string();
                const string& bv = B.vals.count(f) ? B.vals.at(f) : string();
                if (av == bv) continue;
                if (regex_match(av, regex(R"(^-?\d+(\.\d+)?$)")) &&
                    regex_match(bv, regex(R"(^-?\d+(\.\d+)?$)"))) {
                    double da = stod(av), db = stod(bv);
                    return os.asc ? (da < db) : (da > db);
                }
                if (regex_match(av, regex(R"(\d{4}-\d{2}-\d{2})")) &&
                    regex_match(bv, regex(R"(\d{4}-\d{2}-\d{2})"))) {
                    time_t ta = parseDate(av), tb = parseDate(bv);
                    return os.asc ? (ta < tb) : (ta > tb);
                }
                return os.asc ? (av < bv) : (av > bv);
            }
            return false;
        });
    }

    // 6. Build JSON output
    crow::json::wvalue out;
    int idx = 0;
    for (const auto& row : rows) {
        crow::json::wvalue obj;
        if (q.selectCols.empty()) {
            for (const auto& [f, v] : row.vals)
                obj[f] = v;
        } else {
            for (const auto& f : q.selectCols)
                obj[f] = row.vals.count(f) ? row.vals.at(f) : "";
        }
        out[idx++] = std::move(obj);
    }
    return out;
}

