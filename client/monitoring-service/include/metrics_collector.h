#pragma once

#include <string>
#include <vector>
#include <map>
#include <utility>
#include <nlohmann/json.hpp>

class MetricsCollector {
public:
    struct HardwareMetrics {
        std::string device_id;
        std::string readable_date;
        double cpu_usage;
        double memory_usage;
        double disk_usage_root;
        std::string usb_data;
        std::string gpio_state;
        std::string kernel_version;
        std::string hardware_model;
        std::string firmware_version;
    };

    struct Application {
        std::string name;
        std::string version;
    };

    struct SoftwareMetrics {
        std::string device_id;
        std::string readable_date;
        std::string ip_address;
        std::string uptime;
        std::string network_status;
        std::string os_version;
        std::vector<Application> applications;
        std::map<std::string, std::string> services;
    };

    explicit MetricsCollector(const std::string& log_dir);

    // Méthode originale qui retourne les chemins des fichiers
    std::pair<std::string, std::string> collectMetrics();
    
    // Nouvelles méthodes compatibles avec main.cpp
    HardwareMetrics collectHardwareMetrics();
    SoftwareMetrics collectSoftwareMetrics();
    
    // Méthodes de parsing
    HardwareMetrics parseHardwareMetrics(const std::string& file_path);
    SoftwareMetrics parseSoftwareMetrics(const std::string& file_path);
    
    std::string getDeviceId() const;

private:
    std::string log_dir_;
    std::string device_id_;
    
    nlohmann::json readJsonFile(const std::string& file_path);
    void loadDeviceId();
};