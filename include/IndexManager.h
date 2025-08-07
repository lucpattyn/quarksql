// IndexManager.h
#ifndef INDEXMANAGER_H
#define INDEXMANAGER_H

#include <string>
#include <unordered_map>
#include <map>

class IndexManager {
public:
    // table ? ( field ? multimap<value, key> )
    static std::unordered_map<
      std::string,
      std::unordered_map<std::string,
        std::multimap<std::string,std::string>>
    > index;

    static bool hasIndex(const std::string &table,
                         const std::string &field);

    static void rebuildAll();
    static void add(const std::string &table,
                    const std::string &key,
                    const std::map<std::string,std::string> &row,
                    const std::map<std::string,std::string> &oldRow);
    static void remove(const std::string &table,
                       const std::string &key,
                       const std::map<std::string,std::string> &oldRow);
};

#endif // INDEXMANAGER_H

