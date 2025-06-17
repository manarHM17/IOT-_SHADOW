#pragma once
#include <mysql/mysql.h>
#include <string>
#include <vector>

struct DeviceData {
    int id = 0;
    std::string hostname;
    std::string password_hash;
    std::string user;
    std::string location;
    std::string hardware_type;
    std::string os_type;
    std::string created_at;
    std::string updated_at;
    std::string token;
};

class DBHandler {
public:
    DBHandler();
    ~DBHandler();
    
    MYSQL* getConnection();
    bool executeQuery(const std::string& query);
    MYSQL_RES* executeSelect(const std::string& query);
    std::string getLastError();
    void clearPreviousResults();

    // Device management methods
    bool authenticateDevice(const std::string& hostname, const std::string& password);
    std::vector<DeviceData> getAllDevices();
    DeviceData getDeviceById(int device_id);
    bool hostnameExists(const std::string& hostname);
    int addDevice(const DeviceData& device);
    bool deleteDevice(int device_id);
    bool updateDevice(int device_id, const DeviceData& device);
    DeviceData getDeviceByHostname(const std::string& hostname);

    // OTA/General methods
    bool InitializeDatabase();
    bool Execute(const std::string& query);
    MYSQL_RES* Query(const std::string& query);

private:
    MYSQL* conn;
    std::string host = "127.0.0.1";
    std::string user = "root";
    std::string pass = "root";
    std::string db_name = "IOTSHADOW";
    std::string hashPassword(const std::string& password);
};
