// provision-service/include/ConfigManager.h
#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <fstream>
#include <iostream>
#include <filesystem>

class ConfigManager {
private:
    static const std::string CONFIG_DIR;
    static const std::string DEVICE_CONFIG_FILE;
    static const std::string CREDENTIALS_FILE;

public:
    // Device information management
    static bool saveDeviceInfo(const std::string& hostname, const std::string& device_id);
    static bool loadDeviceInfo(const std::string& hostname, std::string& device_id);
    
    // Credentials management for auto-authentication
    static bool saveCredentials(const std::string& hostname, const std::string& password);
    static bool loadCredentials(std::string& hostname, std::string& password);
    static bool clearCredentials();
    
    // Configuration file management
    static bool createConfigDir();
    static bool configExists();
    
    // Utility methods
    static std::string encryptCredentials(const std::string& data);
    static std::string decryptCredentials(const std::string& encrypted_data);
};

#endif // CONFIG_MANAGER_H