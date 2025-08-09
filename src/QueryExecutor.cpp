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

    // Generic path: separate base-table conditions from joined-table ones
    std::vector<Condition> baseConds;
    std::vector<Condition> postConds;
    for (auto &c : q.conditions) {
        auto dot = c.key.find('.');
        if (dot == std::string::npos) {
            baseConds.push_back(c);
        } else {
            std::string qual = c.key.substr(0, dot);
            std::string field = c.key.substr(dot+1);
            if (qual == q.table) {
                Condition cc = c; cc.key = field; baseConds.push_back(cc);
            } else {
                postConds.push_back(c);
            }
        }
    }
    // Scan base table with only base conditions
    auto keys = mgr.scan(q.table, baseConds, q.skip, q.limit);
    // 1) fetch rows
    std::vector<std::map<std::string,std::string>> rows;
    rows.reserve(keys.size());
    for (auto &k:keys)
        rows.push_back(mgr.get(q.table,k));

    // 2) JOINs
    for (auto &j:q.joins) {
        std::vector<std::map<std::string,std::string>> merged;
        for (auto &row:rows) {
            auto lval = row.count(j.leftField) ? row.at(j.leftField) : std::string();
            bool matched = false;
            if (IndexManager::hasIndex(j.rightTable,j.rightField)) {
                auto &mm = IndexManager::index[j.rightTable][j.rightField];
                auto range = mm.equal_range(lval);
                for (auto it=range.first; it!=range.second; ++it) {
                    auto rd = mgr.get(j.rightTable,it->second);
                    auto mrow = row; mrow.insert(rd.begin(),rd.end());
                    merged.push_back(mrow);
                    matched = true;
                }
            } else {
                auto all = mgr.scan(j.rightTable,{},0,-1);
                for (auto &rk:all) {
                    auto rd = mgr.get(j.rightTable,rk);
                    if (rd[j.rightField]==lval) {
                        auto mrow=row; mrow.insert(rd.begin(),rd.end());
                        merged.push_back(mrow);
                        matched = true;
                    }
                }
            }
            if (!matched && j.type==Join::LEFT) {
                // Preserve left row even if no right match
                merged.push_back(row);
            }
        }
        rows.swap(merged);
    }

    // 3) reapply WHERE (for joins and any qualified conditions)
    if (!postConds.empty()) {
        std::vector<decltype(rows)::value_type> filt;
        for (auto &row:rows) {
            bool ok=true;
            for (auto &c:postConds) {
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

    // 4) GROUP BY with aggregates or COUNT or projection
    if (!q.groupBy.empty()) {
        auto gb = q.groupBy;
        if (auto p=gb.find('.'); p!=std::string::npos)
            gb = gb.substr(p+1);

        // If SUM aggregates are requested, compute them per group
        if (!q.aggs.empty()) {
            struct AggAcc { std::unordered_map<std::string,double> sums; };
            std::map<std::string, AggAcc> accs;
            for (auto &r0:rows) {
                std::string key = r0[gb];
                auto &acc = accs[key];
                for (auto &a : q.aggs) {
                    // Support qualified names in aggregates (e.g., l.debit)
                    auto fld = a.field;
                    if (auto p=fld.find('.'); p!=std::string::npos) fld = fld.substr(p+1);
                    double v = 0.0; try { v = std::stod(r0.count(fld)? r0.at(fld) : std::string()); } catch (...) { v = 0.0; }
                    acc.sums[a.alias.empty()? a.field : a.alias] += v;
                }
            }
            for (auto &kv : accs) {
                QueryResultRow o;
                o.vals[gb] = kv.first;
                for (auto &s : kv.second.sums) {
                    o.vals[s.first] = std::to_string(s.second);
                }
                r.rows.push_back(o);
            }
            if (!q.orderByField.empty()) {
                auto fld = q.orderByField;
                if (auto p=fld.find('.'); p!=std::string::npos)
                    fld = fld.substr(p+1);
                std::sort(r.rows.begin(), r.rows.end(), [&](const auto &A, const auto &B){
                    const auto &va = A.vals.count(fld) ? A.vals.at(fld) : std::string();
                    const auto &vb = B.vals.count(fld) ? B.vals.at(fld) : std::string();
                    return q.orderDesc ? (va > vb) : (va < vb);
                });
            }
        } else {
            std::map<std::string,int> grp;
            for (auto &r0:rows) grp[r0[gb]]++;
            for (auto &p:grp) {
                QueryResultRow o;
                o.vals[gb] = p.first;
                o.vals["count"] = std::to_string(p.second);
                r.rows.push_back(o);
            }
        }
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
