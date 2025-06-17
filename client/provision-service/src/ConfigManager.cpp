// provision-service/src/ConfigManager.cpp
#include "../include/ConfigManager.h"
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>

const std::string ConfigManager::CONFIG_DIR = "../config/";
const std::string ConfigManager::DEVICE_CONFIG_FILE = CONFIG_DIR + "device.conf";
const std::string ConfigManager::CREDENTIALS_FILE = CONFIG_DIR + "credentials.conf";

bool ConfigManager::createConfigDir() {
    try {
        if (!std::filesystem::exists(CONFIG_DIR)) {
            return std::filesystem::create_directories(CONFIG_DIR);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error creating config directory: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::saveDeviceInfo(const std::string& hostname, const std::string& device_id) {
    if (!createConfigDir()) {
        return false;
    }
    
    std::ofstream file(DEVICE_CONFIG_FILE);
    if (!file.is_open()) {
        std::cerr << "Failed to open device config file for writing" << std::endl;
        return false;
    }
    
    file << "hostname=" << hostname << std::endl;
    file << "device_id=" << device_id << std::endl;
    file.close();
    
    return true;
}

bool ConfigManager::loadDeviceInfo(const std::string& hostname, std::string& device_id) {
    std::ifstream file(DEVICE_CONFIG_FILE);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    std::string stored_hostname;
    
    while (std::getline(file, line)) {
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            if (key == "hostname") {
                stored_hostname = value;
            } else if (key == "device_id") {
                device_id = value;
            }
        }
    }
    
    file.close();
    
    // Check if hostname matches
    return (stored_hostname == hostname && !device_id.empty());
}

bool ConfigManager::saveCredentials(const std::string& hostname, const std::string& password) {
    if (!createConfigDir()) {
        return false;
    }
    
    std::ofstream file(CREDENTIALS_FILE);
    if (!file.is_open()) {
        std::cerr << "Failed to open credentials file for writing" << std::endl;
        return false;
    }
    
    // Simple encoding (in production, use proper encryption)
    std::string encoded_data = encryptCredentials(hostname + ":" + password);
    file << encoded_data << std::endl;
    file.close();
    
    // Set file permissions to 600 (owner read/write only)
    std::filesystem::permissions(CREDENTIALS_FILE, 
                               std::filesystem::perms::owner_read | 
                               std::filesystem::perms::owner_write);
    
    return true;
}

bool ConfigManager::loadCredentials(std::string& hostname, std::string& password) {
    std::ifstream file(CREDENTIALS_FILE);
    if (!file.is_open()) {
        return false;
    }
    
    std::string encoded_data;
    if (!std::getline(file, encoded_data)) {
        file.close();
        return false;
    }
    
    file.close();
    
    // Decode credentials
    std::string decoded_data = decryptCredentials(encoded_data);
    size_t pos = decoded_data.find(':');
    
    if (pos != std::string::npos) {
        hostname = decoded_data.substr(0, pos);
        password = decoded_data.substr(pos + 1);
        return true;
    }
    
    return false;
}

bool ConfigManager::clearCredentials() {
    try {
        if (std::filesystem::exists(CREDENTIALS_FILE)) {
            return std::filesystem::remove(CREDENTIALS_FILE);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error clearing credentials: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::configExists() {
    return std::filesystem::exists(DEVICE_CONFIG_FILE) || 
           std::filesystem::exists(CREDENTIALS_FILE);
}

std::string ConfigManager::encryptCredentials(const std::string& data) {
    // Simple Base64-like encoding for demonstration
    // In production, use proper AES encryption
    std::string encoded;
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    for (size_t i = 0; i < data.length(); ++i) {
        encoded += chars[(data[i] + 13) % chars.length()];
    }
    
    return encoded;
}

std::string ConfigManager::decryptCredentials(const std::string& encoded_data) {
    // Simple decoding (reverse of encoding)
    std::string decoded;
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    for (size_t i = 0; i < encoded_data.length(); ++i) {
        size_t pos = chars.find(encoded_data[i]);
        if (pos != std::string::npos) {
            decoded += chars[(pos - 13 + chars.length()) % chars.length()];
        }
    }
    
    return decoded;
}