#pragma once
// This file has been replaced by the shared server/common/include/db_handler.h
// Please update your includes to use the shared header.

#include <mysql/mysql.h>
#include <string>

class DBHandler {
public:
    DBHandler();  // Changed constructor to take no parameters
    ~DBHandler();

    bool Connect();
    bool InitializeDatabase();
    bool Execute(const std::string& query);
    MYSQL_RES* Query(const std::string& query);
    MYSQL* GetConnection();

private:
    MYSQL* conn;
    std::string host, user, pass, db_name;
};