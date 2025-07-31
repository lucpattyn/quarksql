#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <map>

using KeyList = std::vector<std::string>;

struct IndexManager {
    // index[table][field][value] -> list of keys
    static std::unordered_map<std::string,
        std::unordered_map<std::string,
            std::unordered_map<std::string,KeyList>>> index;

    static void rebuildAll();
    static void add(const std::string& table,
                    const std::string& key,
                    const std::map<std::string,std::string>& row);
    static void remove(const std::string& table,
                       const std::string& key);
};
