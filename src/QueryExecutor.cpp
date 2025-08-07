// QueryExecutor.cpp
#include "QueryExecutor.h"
#include "SqlParser.h"
#include "DBManager.h"
#include "IndexManager.h"
#include <algorithm>

static bool evalCond(const std::string &lhs,
                     const std::string &op,
                     const std::string &rhs)
{
    if (op=="=")  return lhs==rhs;
    if (op=="!=") return lhs!=rhs;
    if (op=="<")  return lhs<rhs;
    if (op==">")  return lhs>rhs;
    if (op=="<=") return lhs<=rhs;
    if (op==">=") return lhs>=rhs;
    if (op=="LIKE") return lhs.find(rhs)!=std::string::npos;
    return false;
}

void QueryExecutor::execute(const Query &q, QueryResult &r) {
	switch (q.type) {
      case QueryType::INSERT: handleInsert(q,r); break;
      case QueryType::UPDATE: handleUpdate(q,r); break;
      case QueryType::DELETE: handleDelete(q,r); break;
      case QueryType::BATCH:  handleBatch (q,r); break;
      case QueryType::SELECT: handleSelect(q,r); break;
    }
    
    q.print();
}

void QueryExecutor::execute(std::string sql, QueryResult& r){
	auto q = SqlParser::parse(sql);
	execute(q, r);
}

void QueryExecutor::handleInsert(const Query &q, QueryResult &r) {
	auto& mgr = DBManager::instance();
    mgr.insert(q.table, q.rowData);
    r.affected = 1;
}

void QueryExecutor::handleUpdate(const Query &q, QueryResult &r) {
    auto& mgr = DBManager::instance();
	auto keys = mgr.scan(q.table, q.conditions, 0, -1);
    int cnt=0;
    for (auto &k:keys) {
        mgr.update(q.table, k, q.rowData);
        ++cnt;
    }
    r.affected = cnt;
}

void QueryExecutor::handleDelete(const Query &q, QueryResult &r) {
    auto& mgr = DBManager::instance();
	int cnt=0;
    if (!q.deleteKeys.empty()) {
        for (auto &k:q.deleteKeys) {
            mgr.remove(q.table,k);
            ++cnt;
        }
    } else {
        auto keys = mgr.scan(q.table, q.conditions, 0, -1);
        for (auto &k:keys) {
            mgr.remove(q.table,k);
            ++cnt;
        }
    }
    r.affected = cnt;
}

void QueryExecutor::handleBatch(const Query &q, QueryResult &r) {
    auto& mgr = DBManager::instance();
	int cnt=0;
    for (auto &row:q.batchData) {
        mgr.insert(q.table,row);
        ++cnt;
    }
    r.affected = cnt;
}

void QueryExecutor::handleSelect(const Query &q, QueryResult &r) {
    auto& mgr = DBManager::instance();
	// If we can push ORDER BY into an index (no joins/group/count):
    if (q.joins.empty() && q.groupBy.empty() && !q.isCount
        && !q.orderByField.empty()
        && IndexManager::hasIndex(q.table,q.orderByField)
        && q.conditions.empty())
    {
        auto &mmap = IndexManager::index[q.table][q.orderByField];
        int seen=0, taken=0;
        if (!q.orderDesc) {
            for (auto it=mmap.begin(); it!=mmap.end(); ++it) {
                if (seen++ < q.skip) continue;
                r.rows.push_back({ { } });
                r.rows.back().vals =
                   mgr.get(q.table, it->second);
                if (++taken==q.limit) break;
            }
        } else {
            for (auto it=mmap.rbegin(); it!=mmap.rend(); ++it) {
                if (seen++ < q.skip) continue;
                r.rows.push_back({ { } });
                r.rows.back().vals =
                   mgr.get(q.table, it->second);
                if (++taken==q.limit) break;
            }
        }
        r.affected = (int)r.rows.size();
        return;
    }

    // Generic path: scan with skip/limit
    auto keys = mgr.scan(q.table, q.conditions, q.skip, q.limit);
    // 1) fetch rows
    std::vector<std::map<std::string,std::string>> rows;
    rows.reserve(keys.size());
    for (auto &k:keys)
        rows.push_back(mgr.get(q.table,k));

    // 2) JOINs
    for (auto &j:q.joins) {
        std::vector<std::map<std::string,std::string>> merged;
        for (auto &row:rows) {
            auto lval = row[j.leftField];
            if (IndexManager::hasIndex(j.rightTable,j.rightField)) {
                auto &mm = IndexManager::index[j.rightTable][j.rightField];
                auto range = mm.equal_range(lval);
                for (auto it=range.first; it!=range.second; ++it) {
                    auto rd = mgr.get(j.rightTable,it->second);
                    auto mrow = row; mrow.insert(rd.begin(),rd.end());
                    merged.push_back(mrow);
                }
            } else {
                auto all = mgr.scan(j.rightTable,{},0,-1);
                for (auto &rk:all) {
                    auto rd = mgr.get(j.rightTable,rk);
                    if (rd[j.rightField]==lval) {
                        auto mrow=row; mrow.insert(rd.begin(),rd.end());
                        merged.push_back(mrow);
                    }
                }
            }
        }
        rows.swap(merged);
    }

    // 3) reapply WHERE (for joins)
    if (!q.conditions.empty()) {
        std::vector<decltype(rows)::value_type> filt;
        for (auto &row:rows) {
            bool ok=true;
            for (auto &c:q.conditions) {
                auto key = c.key;
                if (auto p=key.find('.'); p!=std::string::npos)
                    key = key.substr(p+1);
                if (!evalCond(row[key], c.op, c.value)) {
                    ok=false; break;
                }
            }
            if (ok) filt.push_back(row);
        }
        rows.swap(filt);
    }

    // 4) GROUP BY or COUNT or projection
    if (!q.groupBy.empty()) {
        std::map<std::string,int> grp;
        auto gb = q.groupBy;
        if (auto p=gb.find('.'); p!=std::string::npos)
            gb = gb.substr(p+1);
        for (auto &r0:rows) grp[r0[gb]]++;
        for (auto &p:grp) {
            QueryResultRow o;
            o.vals[q.groupBy] = p.first;
            o.vals["count"] = std::to_string(p.second);
            r.rows.push_back(o);
        }
    }
    else if (q.isCount) {
        QueryResultRow o;
        o.vals["count"] = std::to_string(rows.size());
        r.rows.push_back(o);
    }
    else {
        bool wild = (q.selectCols.size()==1 && q.selectCols[0]=="*");
        for (auto &r0:rows) {
            QueryResultRow o;
            if (wild) {
                for (auto &kv:r0) o.vals[kv.first]=kv.second;
            } else {
                for (auto &col:q.selectCols) {
                    auto fld = col;
                    if (auto p=fld.find('.'); p!=std::string::npos)
                        fld = fld.substr(p+1);
                    o.vals[col] = r0[fld];
                }
            }
            r.rows.push_back(o);
        }
    }

    // 5) ORDER BY (if not optimized above)
    if (!q.orderByField.empty()) {
        auto fld=q.orderByField;
        if (auto p=fld.find('.'); p!=std::string::npos)
            fld=fld.substr(p+1);
        std::sort(r.rows.begin(),r.rows.end(),
          [&](auto &a,auto &b) {
            return q.orderDesc
              ? b.vals[fld] < a.vals[fld]
              : a.vals[fld] < b.vals[fld];
          });
    }

    // 6) SKIP/LIMIT if not already applied
    if (!q.joins.empty() || !q.groupBy.empty() || q.isCount) {
        auto &v = r.rows;
        int start = std::min((int)v.size(), q.skip);
        int end   = (q.limit>=0)
                    ? std::min((int)v.size(), start+q.limit)
                    : (int)v.size();
        std::vector<QueryResultRow> slice(v.begin()+start, v.begin()+end);
        r.rows.swap(slice);
    }

    r.affected = (int)r.rows.size();
}

