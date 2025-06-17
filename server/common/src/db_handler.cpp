#include "../include/db_handler.h"
#include <stdexcept>
#include <iostream>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <regex>

DBHandler::DBHandler() {
    conn = mysql_init(nullptr);
    if (!conn) {
        throw std::runtime_error("MySQL initialization failed");
    }
    // Connect without database first
    if (!mysql_real_connect(conn, host.c_str(), user.c_str(), pass.c_str(), nullptr, 3306, nullptr, 0)) {
        std::string err = mysql_error(conn);
        mysql_close(conn);
        throw std::runtime_error("MySQL connection failed: " + err);
    }
    // Create database if not exists
    if (mysql_query(conn, "CREATE DATABASE IF NOT EXISTS IOTSHADOW")) {
        std::string error = mysql_error(conn);
        mysql_close(conn);
        throw std::runtime_error("Failed to create database: " + error);
    }
    // Select the database
    if (mysql_select_db(conn, db_name.c_str())) {
        std::string error = mysql_error(conn);
        mysql_close(conn);
        throw std::runtime_error("Failed to select database: " + error);
    }
    InitializeDatabase();
}

DBHandler::~DBHandler() {
    if (conn) {
        mysql_close(conn);
    }
}

MYSQL* DBHandler::getConnection() {
    if (!conn) {
        throw std::runtime_error("MySQL connection is not initialized");
    }
    clearPreviousResults();
    return conn;
}

bool DBHandler::executeQuery(const std::string& query) {
    clearPreviousResults();
    if (mysql_query(conn, query.c_str()) != 0) {
        std::cerr << "MySQL query error: " << mysql_error(conn) << std::endl;
        return false;
    }
    return true;
}

MYSQL_RES* DBHandler::executeSelect(const std::string& query) {
    clearPreviousResults();
    if (mysql_query(conn, query.c_str()) != 0) {
        std::cerr << "MySQL query error: " << mysql_error(conn) << std::endl;
        return nullptr;
    }
    return mysql_store_result(conn);
}

bool DBHandler::InitializeDatabase() {
    // OTA/updates tables
    std::string updates_table = R"(
        CREATE TABLE IF NOT EXISTS updates (
            id INT AUTO_INCREMENT PRIMARY KEY,
            app_name VARCHAR(255),
            version VARCHAR(64),
            file_path VARCHAR(512),
            checksum VARCHAR(128)
        )
    )";
    std::string status_table = R"(
        CREATE TABLE IF NOT EXISTS update_status (
            id INT AUTO_INCREMENT PRIMARY KEY,
            device_id INT,
            app_name VARCHAR(255),
            current_version VARCHAR(64),
            target_version VARCHAR(64),
            status VARCHAR(32),
            error_message TEXT,
            last_update TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    )";
    bool ok1 = Execute(updates_table);
    bool ok2 = Execute(status_table);
    // Devices table (provision)
    const char* create_devices_table_query =
        "CREATE TABLE IF NOT EXISTS devices ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "hostname VARCHAR(255) NOT NULL UNIQUE,"
        "password_hash VARCHAR(255) NOT NULL,"
        "user VARCHAR(255) NOT NULL,"
        "location VARCHAR(255),"
        "hardware_type VARCHAR(255) NOT NULL,"
        "os_type VARCHAR(255) NOT NULL,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "token VARCHAR(512),"
        "INDEX idx_hostname (hostname)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";
    bool ok3 = Execute(create_devices_table_query);
    return ok1 && ok2 && ok3;
}

bool DBHandler::Execute(const std::string& query) {
    if (mysql_query(conn, query.c_str())) {
        std::cerr << "MySQL query failed: " << mysql_error(conn) << std::endl;
        return false;
    }
    return true;
}

MYSQL_RES* DBHandler::Query(const std::string& query) {
    if (mysql_query(conn, query.c_str())) {
        std::cerr << "MySQL query failed: " << mysql_error(conn) << std::endl;
        return nullptr;
    }
    return mysql_store_result(conn);
}

bool DBHandler::authenticateDevice(const std::string& hostname, const std::string& password) {
    char escaped_hostname[hostname.length()*2+1];
    mysql_real_escape_string(conn, escaped_hostname, hostname.c_str(), hostname.length());
    std::string query = "SELECT password_hash FROM devices WHERE hostname = '" + std::string(escaped_hostname) + "'";
    MYSQL_RES* result = executeSelect(query);
    if (!result) return false;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
        mysql_free_result(result);
        return false;
    }
    std::string stored_hash = row[0];
    mysql_free_result(result);
    return stored_hash == hashPassword(password);
}

std::vector<DeviceData> DBHandler::getAllDevices() {
    std::vector<DeviceData> devices;
    const char* query = "SELECT id, hostname, user, location, hardware_type, os_type, created_at, updated_at FROM devices";
    MYSQL_RES* result = executeSelect(query);
    if (!result) return devices;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        DeviceData device;
        device.id = std::stoi(row[0]);
        device.hostname = row[1] ? row[1] : "";
        device.user = row[2] ? row[2] : "";
        device.location = row[3] ? row[3] : "";
        device.hardware_type = row[4] ? row[4] : "";
        device.os_type = row[5] ? row[5] : "";
        device.created_at = row[6] ? row[6] : "";
        device.updated_at = row[7] ? row[7] : "";
        devices.push_back(device);
    }
    mysql_free_result(result);
    return devices;
}

DeviceData DBHandler::getDeviceById(int device_id) {
    DeviceData device;
    std::string query = "SELECT id, hostname, user, location, hardware_type, os_type, created_at, updated_at FROM devices WHERE id = " + std::to_string(device_id);
    MYSQL_RES* result = executeSelect(query);
    if (!result) return device;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row) {
        device.id = std::stoi(row[0]);
        device.hostname = row[1] ? row[1] : "";
        device.user = row[2] ? row[2] : "";
        device.location = row[3] ? row[3] : "";
        device.hardware_type = row[4] ? row[4] : "";
        device.os_type = row[5] ? row[5] : "";
        device.created_at = row[6] ? row[6] : "";
        device.updated_at = row[7] ? row[7] : "";
    }
    mysql_free_result(result);
    return device;
}

DeviceData DBHandler::getDeviceByHostname(const std::string& hostname) {
    DeviceData device;
    char escaped_hostname[hostname.length()*2+1];
    mysql_real_escape_string(conn, escaped_hostname, hostname.c_str(), hostname.length());
    std::string query = "SELECT id, hostname, user, location, hardware_type, os_type, created_at, updated_at FROM devices WHERE hostname = '" + std::string(escaped_hostname) + "'";
    MYSQL_RES* result = executeSelect(query);
    if (!result) return device;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row) {
        device.id = std::stoi(row[0]);
        device.hostname = row[1] ? row[1] : "";
        device.user = row[2] ? row[2] : "";
        device.location = row[3] ? row[3] : "";
        device.hardware_type = row[4] ? row[4] : "";
        device.os_type = row[5] ? row[5] : "";
        device.created_at = row[6] ? row[6] : "";
        device.updated_at = row[7] ? row[7] : "";
    }
    mysql_free_result(result);
    return device;
}

bool DBHandler::hostnameExists(const std::string& hostname) {
    char escaped_hostname[hostname.length()*2+1];
    mysql_real_escape_string(conn, escaped_hostname, hostname.c_str(), hostname.length());
    std::string query = "SELECT COUNT(*) FROM devices WHERE hostname = '" + std::string(escaped_hostname) + "'";
    MYSQL_RES* result = executeSelect(query);
    if (!result) return false;
    MYSQL_ROW row = mysql_fetch_row(result);
    bool exists = row && std::stoi(row[0]) > 0;
    mysql_free_result(result);
    return exists;
}

int DBHandler::addDevice(const DeviceData& device) {
    char escaped_hostname[device.hostname.length()*2+1];
    char escaped_user[device.user.length()*2+1];
    char escaped_location[device.location.length()*2+1];
    char escaped_hw_type[device.hardware_type.length()*2+1];
    char escaped_os_type[device.os_type.length()*2+1];
    mysql_real_escape_string(conn, escaped_hostname, device.hostname.c_str(), device.hostname.length());
    mysql_real_escape_string(conn, escaped_user, device.user.c_str(), device.user.length());
    mysql_real_escape_string(conn, escaped_location, device.location.c_str(), device.location.length());
    mysql_real_escape_string(conn, escaped_hw_type, device.hardware_type.c_str(), device.hardware_type.length());
    mysql_real_escape_string(conn, escaped_os_type, device.os_type.c_str(), device.os_type.length());
    std::string hashed_password = hashPassword(device.password_hash);
    std::string query = "INSERT INTO devices (hostname, password_hash, user, location, hardware_type, os_type) VALUES ('" +
        std::string(escaped_hostname) + "', '" +
        hashed_password + "', '" +
        std::string(escaped_user) + "', '" +
        std::string(escaped_location) + "', '" +
        std::string(escaped_hw_type) + "', '" +
        std::string(escaped_os_type) + "')";
    if (!executeQuery(query)) {
        return 0;
    }
    return mysql_insert_id(conn);
}

bool DBHandler::deleteDevice(int device_id) {
    std::string query = "DELETE FROM devices WHERE id = " + std::to_string(device_id);
    return executeQuery(query);
}

bool DBHandler::updateDevice(int device_id, const DeviceData& device) {
    char escaped_user[device.user.length()*2+1];
    char escaped_location[device.location.length()*2+1];
    char escaped_hw_type[device.hardware_type.length()*2+1];
    char escaped_os_type[device.os_type.length()*2+1];
    mysql_real_escape_string(conn, escaped_user, device.user.c_str(), device.user.length());
    mysql_real_escape_string(conn, escaped_location, device.location.c_str(), device.location.length());
    mysql_real_escape_string(conn, escaped_hw_type, device.hardware_type.c_str(), device.hardware_type.length());
    mysql_real_escape_string(conn, escaped_os_type, device.os_type.c_str(), device.os_type.length());
    std::string query = "UPDATE devices SET "
        "user = '" + std::string(escaped_user) + "', "
        "location = '" + std::string(escaped_location) + "', "
        "hardware_type = '" + std::string(escaped_hw_type) + "', "
        "os_type = '" + std::string(escaped_os_type) + "' "
        "WHERE id = " + std::to_string(device_id);
    return executeQuery(query);
}

std::string DBHandler::hashPassword(const std::string& password) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }
    if (!EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr)) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize digest");
    }
    if (!EVP_DigestUpdate(ctx, password.c_str(), password.length())) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to update digest");
    }
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen;
    if (!EVP_DigestFinal_ex(ctx, hash, &hashLen)) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize digest");
    }
    EVP_MD_CTX_free(ctx);
    std::stringstream ss;
    for(unsigned int i = 0; i < hashLen; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

std::string DBHandler::getLastError() {
    return mysql_error(conn);
}

void DBHandler::clearPreviousResults() {
    MYSQL_RES* result;
    while ((result = mysql_store_result(conn)) != nullptr) {
        mysql_free_result(result);
    }
    while (mysql_next_result(conn) == 0) {
        result = mysql_store_result(conn);
        if (result) {
            mysql_free_result(result);
        }
    }
}
