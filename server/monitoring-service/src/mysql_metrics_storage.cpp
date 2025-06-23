#include "mysql_metrics_storage.h"
#include <mysql/mysql.h>
#include <iostream>

MySQLMetricsStorage::MySQLMetricsStorage() {
    conn_ = mysql_init(nullptr);
    
    // Première connexion sans sélectionner de base
    std::cout << "Attempting to connect to MySQL..." << std::endl;
    if (!mysql_real_connect(static_cast<MYSQL*>(conn_), "127.0.0.1", "root", "root", nullptr, 0, nullptr, 0)) {
        std::cerr << "MySQL connection failed: " << mysql_error(static_cast<MYSQL*>(conn_)) << std::endl;
        conn_ = nullptr;
        return;
    }
    std::cout << "Connected to MySQL successfully" << std::endl;

    initDatabase();
}

MySQLMetricsStorage::~MySQLMetricsStorage() {
    if (conn_) mysql_close(static_cast<MYSQL*>(conn_));
}

bool MySQLMetricsStorage::reconnect() {
    std::lock_guard<std::mutex> lock(mysql_mutex_);
    
    if (conn_) {
        mysql_close(static_cast<MYSQL*>(conn_));
    }
    
    conn_ = mysql_init(nullptr);
    if (!conn_) return false;

    // Enable auto-reconnect
    bool reconnect_flag = true;
    mysql_options(static_cast<MYSQL*>(conn_), MYSQL_OPT_RECONNECT, &reconnect_flag);
    
    if (!mysql_real_connect(static_cast<MYSQL*>(conn_), "127.0.0.1", "root", "root", nullptr, 0, nullptr, 0)) {
        std::cerr << "MySQL reconnection failed: " << mysql_error(static_cast<MYSQL*>(conn_)) << std::endl;
        return false;
    }

    return initDatabase();
}
bool MySQLMetricsStorage::initDatabase() {
    if (!conn_) return false;

    // Créer la base si elle n'existe pas
    std::cout << "Creating database if not exists..." << std::endl;
    if (mysql_query(static_cast<MYSQL*>(conn_), "CREATE DATABASE IF NOT EXISTS IOTSHADOW")) {
        std::cerr << "Failed to create database: " << mysql_error(static_cast<MYSQL*>(conn_)) << std::endl;
        return false;
    }

    // Sélectionner la base
    std::cout << "Selecting database IOTSHADOW..." << std::endl;
    if (mysql_select_db(static_cast<MYSQL*>(conn_), "IOTSHADOW")) {
        std::cerr << "Failed to select database: " << mysql_error(static_cast<MYSQL*>(conn_)) << std::endl;
        return false;
    }

    // Création des tables - Updated schema to match data types
    const char* create_hw_table =
        "CREATE TABLE IF NOT EXISTS hardware_info ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "device_id VARCHAR(128),"
        "readable_date VARCHAR(32),"
        "cpu_usage DECIMAL(5,2),"  
        "memory_usage DECIMAL(5,2),"  
        "disk_usage DECIMAL(5,2),"  
        "usb_state TEXT,"
        "gpio_state INT,"  // Changed from VARCHAR(32) to INT
        "kernel_version VARCHAR(64),"
        "hardware_model VARCHAR(128),"
        "firmware_version VARCHAR(128),"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")";

    if (mysql_query(static_cast<MYSQL*>(conn_), create_hw_table)) {
        std::cerr << "Failed to create hardware_info table: " << mysql_error(static_cast<MYSQL*>(conn_)) << std::endl;
        return false;
    }

    // Rest of the function (software table creation) remains unchanged
    const char* create_sw_table =
        "CREATE TABLE IF NOT EXISTS software_info ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "device_id VARCHAR(128),"
        "readable_date VARCHAR(32),"
        "ip_address VARCHAR(64),"
        "uptime VARCHAR(64),"
        "network_status VARCHAR(32),"
        "os_version VARCHAR(128),"
        "applications TEXT,"
        "services TEXT,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")";

    if (mysql_query(static_cast<MYSQL*>(conn_), create_sw_table)) {
        std::cerr << "Failed to create software_info table: " << mysql_error(static_cast<MYSQL*>(conn_)) << std::endl;
        return false;
    }
    return true;
}
bool MySQLMetricsStorage::executeQuery(const std::string& query) {
    std::lock_guard<std::mutex> lock(mysql_mutex_);
    
    if (!conn_) {
        if (!reconnect()) return false;
    }

    if (mysql_query(static_cast<MYSQL*>(conn_), query.c_str())) {
        // Check if connection was lost
        if (mysql_errno(static_cast<MYSQL*>(conn_)) == CR_SERVER_LOST ||
            mysql_errno(static_cast<MYSQL*>(conn_)) == CR_SERVER_GONE_ERROR) {
            // Try to reconnect once
            if (reconnect()) {
                // Retry query after successful reconnection
                if (!mysql_query(static_cast<MYSQL*>(conn_), query.c_str())) {
                    return true;
                }
            }
        }
        std::cerr << "MySQL query error: " << mysql_error(static_cast<MYSQL*>(conn_)) << std::endl;
        return false;
    }
    return true;
}

// Helper function to escape SQL strings
std::string MySQLMetricsStorage::escapeSqlString(const std::string& input) {
    if (!conn_) return input;
    
    char* escaped = new char[input.length() * 2 + 1];
    mysql_real_escape_string(static_cast<MYSQL*>(conn_), escaped, input.c_str(), input.length());
    std::string result(escaped);
    delete[] escaped;
    return result;
}

bool MySQLMetricsStorage::insertHardwareInfo(const nlohmann::json& m) {
    if (!conn_) {
        std::cerr << "No MySQL connection available" << std::endl;
        return false;
    }

    // Handle string fields
    std::string device_id = escapeSqlString(m.value("device_id", "unknown"));
    std::string readable_date = escapeSqlString(m.value("readable_date", ""));
    std::string usb_state = escapeSqlString(m.value("usb_state", ""));
    std::string kernel_version = escapeSqlString(m.value("kernel_version", ""));
    std::string hardware_model = escapeSqlString(m.value("hardware_model", ""));
    std::string firmware_version = escapeSqlString(m.value("firmware_version", ""));

    // Handle numeric fields
    double cpu_usage = 0.0;
    double memory_usage = 0.0;
    double disk_usage = 0.0;
    int gpio_state = 0;  // Now an integer

    // Extract numeric values
    if (m.contains("cpu_usage") && m["cpu_usage"].is_number()) {
        cpu_usage = m["cpu_usage"].get<double>();
    }
    if (m.contains("memory_usage") && m["memory_usage"].is_number()) {
        memory_usage = m["memory_usage"].get<double>();
    }
    if (m.contains("disk_usage") && m["disk_usage"].is_number()) {
        disk_usage = m["disk_usage"].get<double>();
    }

    // Handle gpio_state as an integer
    if (m.contains("gpio_state")) {
        if (m["gpio_state"].is_number_integer()) {
            gpio_state = m["gpio_state"].get<int>();  // Direct integer from JSON
        } else if (m["gpio_state"].is_string()) {
            try {
                gpio_state = std::stoi(m["gpio_state"].get<std::string>());  // Convert string to int
            } catch (const std::exception& e) {
                std::cerr << "Error converting gpio_state to int: " << e.what() << std::endl;
                gpio_state = 0;  // Default value on failure
            }
        } else {
            std::cerr << "gpio_state is neither a number nor a convertible string" << std::endl;
        }
    }

    // Construct the SQL query
    std::string query =
        "INSERT INTO hardware_info (device_id, readable_date, cpu_usage, memory_usage, disk_usage, usb_state, gpio_state, kernel_version, hardware_model, firmware_version) VALUES ('" +
        device_id + "','" +
        readable_date + "'," +
        std::to_string(cpu_usage) + "," +
        std::to_string(memory_usage) + "," +
        std::to_string(disk_usage) + ",'" +
        usb_state + "'," +
        std::to_string(gpio_state) + "," +  // Insert as number, no quotes
        "'" + kernel_version + "','" +
        hardware_model + "','" +
        firmware_version + "')";

    return executeQuery(query);
}

bool MySQLMetricsStorage::insertSoftwareInfo(const nlohmann::json& m) {
    if (!conn_) {
        std::cerr << "No MySQL connection available" << std::endl;
        return false;
    }

    // Log the full JSON for debugging
    
    std::string apps;
    if (m.contains("applications")) {
        for (const auto& app : m["applications"]) {
            if (!apps.empty()) apps += ";";
            apps += app.value("name", "") + ":" + app.value("version", "");
        }
    }
    
    std::string services;
    if (m.contains("services")) {
        for (auto& [k, v] : m["services"].items()) {
            if (!services.empty()) services += ";";
            services += k + ":" + v.get<std::string>();
        }
    }
    
    // Escape all string values
    std::string device_id = escapeSqlString(m.value("device_id", "unknown"));
    std::string readable_date = escapeSqlString(m.value("readable_date", ""));
    std::string ip_address = escapeSqlString(m.value("ip_address", ""));
    std::string uptime = escapeSqlString(m.value("uptime", ""));
    std::string network_status = escapeSqlString(m.value("network_status", ""));
    std::string os_version = escapeSqlString(m.value("os_version", ""));
    apps = escapeSqlString(apps);
    services = escapeSqlString(services);
    
    std::string query =
        "INSERT INTO software_info (device_id, readable_date, ip_address, uptime, network_status, os_version, applications, services) VALUES ('" +
        device_id + "','" +
        readable_date + "','" +
        ip_address + "','" +
        uptime + "','" +
        network_status + "','" +
        os_version + "','" +
        apps + "','" +
        services + "')";
        
    return executeQuery(query);
}