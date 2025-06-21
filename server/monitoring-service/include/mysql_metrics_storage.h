#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <mutex>

class MySQLMetricsStorage {
public:
    MySQLMetricsStorage();
    ~MySQLMetricsStorage();

    bool insertHardwareInfo(const nlohmann::json& metrics);
    bool insertSoftwareInfo(const nlohmann::json& metrics);
    bool reconnect();
    bool initDatabase();
    bool executeQuery(const std::string& query);

private:
    void* conn_; // Use MYSQL* if you include <mysql/mysql.h>
    std::mutex mysql_mutex_;
    std::string escapeSqlString(const std::string& input);

};
