// File: main.cpp
#include "crow.h"
#include "DBManager.h"
#include "IndexManager.h"
#include "SqlParser.h"
#include "QueryExecutor.h"

#include <rocksdb/write_batch.h>
#include <regex>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <string>


// LUC: 20210731 : ugliest of hacks
int crow::detail::dumb_timer_queue::tick = 5;

// ---- Utility ----
std::string generate_key(const crow::json::rvalue& data) {
    if (data.has("id") && data["id"].t() == crow::json::type::Number)
        return std::to_string(data["id"].i());
    std::ostringstream ss;
    for (const auto& p : data)
        ss << p.key() << '=' << rvalue_to_string(p) << ';';
    return std::to_string(std::hash<std::string>{}(ss.str()));
}


void update_index_helper(const std::string& table, const std::string& key, const crow::json::rvalue& data) {
    std::map<std::string, std::string> row;
    for (auto& p : data) {
        row[p.key()] = rvalue_to_string(p);
    }
    IndexManager::add(table, key, row);
}

void remove_index_helper(const std::string& table, const std::string& key) {
    IndexManager::remove(table, key);
}

// simple single-field = / != clause parser
bool parse_simple_eq(const std::string& clause, std::string& field, std::string& op, std::string& value) {
    std::regex cond_re(R"(^\s*(\w+)\s*(=|!=)\s*'([^']+)'\s*$)", std::regex::icase);
    std::smatch m;
    if (std::regex_match(clause, m, cond_re)) {
        field = m[1].str();
        op = m[2].str();
        value = m[3].str();
        return true;
    }
    return false;
}

// ---- Handlers ----

crow::json::wvalue handle_insert(const crow::json::rvalue& body) {
    std::string table = body["table"].s();
    auto data = body["data"];
    ColumnFamilyHandle* cf = DBManager::instance().cf(table);
    if (!cf) {
        crow::json::wvalue err; err["error"] = "Table error"; return err;
    }
    std::string key = generate_key(data);
    std::string val = crow::json::dump(data);

    rocksdb::WriteBatch batch;
    batch.Put(cf, key, val);
    auto s = DBManager::instance().db()->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) {
        crow::json::wvalue err; err["error"] = s.ToString(); return err;
    }

    {
        std::lock_guard<std::mutex> lk(DBManager::instance().mutex());
        update_index_helper(table, key, data);
    }

    crow::json::wvalue res; res["status"] = "inserted"; res["key"] = key; return res;
}

crow::json::wvalue handle_update(const crow::json::rvalue& body) {
    std::string table = body["table"].s();
    auto data = body["data"];
    std::string clause = body["where"].s();

    if (!DBManager::instance().all_cfs().count(table)) {
        crow::json::wvalue err; err["error"] = "No table"; return err;
    }

    std::set<std::string> keys;
    std::string field, op, value;
    if (parse_simple_eq(clause, field, op, value)) {
        if (op == "=") {
            for (auto& k : IndexManager::index[table][field][value]) keys.insert(k);
        } else if (op == "!=") {
            for (auto& [val, vec] : IndexManager::index[table][field]) {
                if (val != value)
                    keys.insert(vec.begin(), vec.end());
            }
        }
    } else {
        std::string sql = std::string("SELECT * FROM ") + std::string(table) + std::string(" WHERE ") + std::string(clause) + std::string(";");
        auto result = QueryExecutor::execute(SqlParser::parse(sql));
        for (size_t i = 0;; ++i) {
            auto item = std::move(result[i]);
            if (item.t() == crow::json::type::Null) break;
            std::string dumped = crow::json::dump(item);
            auto rv = crow::json::load(dumped);
            if (!rv) continue;
            keys.insert(generate_key(rv));
        }
    }

    if (keys.empty()) {
        crow::json::wvalue res; res["status"] = "updated"; res["count"] = 0; return res;
    }

    rocksdb::WriteBatch batch;
    for (const auto& key : keys) {
        std::string existing_raw;
        DBManager::instance().db()->Get(rocksdb::ReadOptions(), DBManager::instance().cf(table), key, &existing_raw);
        auto existing = crow::json::load(existing_raw);
        if (!existing) continue;
        crow::json::wvalue updated = existing;
        for (auto p : data) updated[p.key()] = p;
        batch.Put(DBManager::instance().cf(table), key, crow::json::dump(updated));
    }
    auto s = DBManager::instance().db()->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) {
        crow::json::wvalue err; err["error"] = s.ToString(); return err;
    }

    {
        std::lock_guard<std::mutex> lk(DBManager::instance().mutex());
        for (const auto& key : keys) {
            std::string raw;
            DBManager::instance().db()->Get(rocksdb::ReadOptions(), DBManager::instance().cf(table), key, &raw);
            auto js = crow::json::load(raw);
            if (js) update_index_helper(table, key, js);
        }
    }

    crow::json::wvalue res; res["status"] = "updated"; res["count"] = (int)keys.size(); return res;
}

crow::json::wvalue handle_delete(const crow::json::rvalue& body) {
    std::string table = body["table"].s();
    std::string clause = body["where"].s();

    if (!DBManager::instance().all_cfs().count(table)) {
        crow::json::wvalue err; err["error"] = "No table"; return err;
    }

    std::set<std::string> keys;
    std::string field, op, value;
    if (parse_simple_eq(clause, field, op, value)) {
        if (op == "=") {
            for (auto& k : IndexManager::index[table][field][value]) keys.insert(k);
        } else if (op == "!=") {
            for (auto& [val, vec] : IndexManager::index[table][field]) {
                if (val != value)
                    keys.insert(vec.begin(), vec.end());
            }
        }
    } else {
        std::string sql = std::string("SELECT * FROM ") + std::string(table) + std::string(" WHERE ") + std::string(clause) + std::string(";");
        auto result = QueryExecutor::execute(SqlParser::parse(sql));
        for (size_t i = 0;; ++i) {
            auto item = std::move(result[i]);
            if (item.t() == crow::json::type::Null) break;
            std::string dumped = crow::json::dump(item);
            auto rv = crow::json::load(dumped);
            if (!rv) continue;
            keys.insert(generate_key(rv));
        }
    }

    if (keys.empty()) {
        crow::json::wvalue res; res["status"] = "deleted"; res["count"] = 0; return res;
    }

    rocksdb::WriteBatch batch;
    for (const auto& key : keys) {
        batch.Delete(DBManager::instance().cf(table), key);
    }
    auto s = DBManager::instance().db()->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) {
        crow::json::wvalue err; err["error"] = s.ToString(); return err;
    }

    {
        std::lock_guard<std::mutex> lk(DBManager::instance().mutex());
        for (const auto& key : keys) remove_index_helper(table, key);
    }

    crow::json::wvalue res; res["status"] = "deleted"; res["count"] = (int)keys.size(); return res;
}

crow::json::wvalue handle_batch(const crow::json::rvalue& body) {
    auto cmds = body["commands"];
    rocksdb::WriteBatch batch;
    std::vector<std::tuple<std::string, std::string, crow::json::rvalue>> todo;

    for (size_t ci = 0; ci < cmds.size(); ++ci) {
        const auto& cmd = cmds[ci];
        auto c = cmd["command"].s();
        if (c == "insert") {
            auto table = cmd["table"].s();
            auto data = cmd["data"];
            if (!DBManager::instance().all_cfs().count(table))
                DBManager::instance().cf(table);
            auto key = generate_key(data);
            batch.Put(DBManager::instance().cf(table), key, crow::json::dump(data));
            todo.emplace_back("insert", table, data);
        } else if (c == "update") {
            auto table = cmd["table"].s();
            auto data = cmd["data"];
            auto clause = cmd["where"].s();
            std::set<std::string> keys;
            std::string field, op, value;
            if (parse_simple_eq(clause, field, op, value)) {
                if (op == "=") {
                    for (auto& k : IndexManager::index[table][field][value]) keys.insert(k);
                } else if (op == "!=") {
                    for (auto& [val, vec] : IndexManager::index[table][field]) {
                        if (val != value)
                            keys.insert(vec.begin(), vec.end());
                    }
                }
            } else {
                std::string sql = std::string("SELECT * FROM ") + std::string(table) + std::string(" WHERE ") + std::string(clause) + std::string(";");
                auto result = QueryExecutor::execute(SqlParser::parse(sql));
                for (size_t i = 0;; ++i) {
                    auto item = std::move(result[i]);
                    if (item.t() == crow::json::type::Null) break;
                    std::string dumped = crow::json::dump(item);
                    auto rv = crow::json::load(dumped);
                    if (!rv) continue;
                    keys.insert(generate_key(rv));
                }
            }
            for (const auto& key : keys) {
                std::string existing_raw;
                DBManager::instance().db()->Get(rocksdb::ReadOptions(), DBManager::instance().cf(table), key, &existing_raw);
                auto existing = crow::json::load(existing_raw);
                if (!existing) continue;
                crow::json::wvalue updated = existing;
                for (auto p : data) updated[p.key()] = p;
                batch.Put(DBManager::instance().cf(table), key, crow::json::dump(updated));
            }
        } else if (c == "delete") {
            auto table = cmd["table"].s();
            auto clause = cmd["where"].s();
            std::set<std::string> keys;
            std::string field, op, value;
            if (parse_simple_eq(clause, field, op, value)) {
                if (op == "=") {
                    for (auto& k : IndexManager::index[table][field][value]) keys.insert(k);
                } else if (op == "!=") {
                    for (auto& [val, vec] : IndexManager::index[table][field]) {
                        if (val != value)
                            keys.insert(vec.begin(), vec.end());
                    }
                }
            } else {
                std::string sql = std::string("SELECT * FROM ") + std::string(table) + std::string(" WHERE ") + std::string(clause) + std::string(";");
                auto result = QueryExecutor::execute(SqlParser::parse(sql));
                for (size_t i = 0;; ++i) {
                    auto item = std::move(result[i]);
                    if (item.t() == crow::json::type::Null) break;
                    std::string dumped = crow::json::dump(item);
                    auto rv = crow::json::load(dumped);
                    if (!rv) continue;
                    keys.insert(generate_key(rv));
                }
            }
            for (const auto& key : keys) {
                batch.Delete(DBManager::instance().cf(table), key);
            }
        }
    }

    auto s = DBManager::instance().db()->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) {
        crow::json::wvalue err; err["error"] = s.ToString(); return err;
    }

    {
        std::lock_guard<std::mutex> lk(DBManager::instance().mutex());
        for (auto& t : todo) {
            const auto& cmd_type = std::get<0>(t);
            const auto& tbl = std::get<1>(t);
            const auto& data = std::get<2>(t);
            auto key = generate_key(data);
            if (cmd_type == "insert") update_index_helper(tbl, key, data);
        }
    }

    crow::json::wvalue res; res["status"] = "batch ok"; return res;
}

// ---- Main ----
int main() {
    crow::SimpleApp app;

    if (!DBManager::instance().init("quarks_db")) return 1;
    IndexManager::rebuildAll();

    CROW_ROUTE(app, "/query").methods("POST"_method)([](const crow::request& req) {
        try {
            auto body = crow::json::load(req.body);
            if (!body) return crow::response(400, "Invalid JSON");
            std::string cmd = body["command"].s();
            if (cmd == "insert") return crow::response(crow::json::dump(handle_insert(body)));
            if (cmd == "select_sql") return crow::response(crow::json::dump(QueryExecutor::execute(SqlParser::parse(body["sql"].s()))));
            if (cmd == "update") return crow::response(crow::json::dump(handle_update(body)));
            if (cmd == "delete") return crow::response(crow::json::dump(handle_delete(body)));
            if (cmd == "batch") return crow::response(crow::json::dump(handle_batch(body)));
            return crow::response(400, "Unsupported command");
        } catch (const std::exception& e) {
            crow::json::wvalue err; err["error"] = e.what();
            return crow::response(500, crow::json::dump(err));
        }
    });

    crow::mustache::set_base("./templates/");
    CROW_ROUTE(app, "/")([]() {
        crow::mustache::context ctx;
        ctx["name"] = "Crow User";
        auto page = crow::mustache::load("index.html");
        return page.render(ctx);
    });

    app.port(18080).multithreaded().run();
    return 0;
}

