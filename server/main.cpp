#include <grpcpp/grpcpp.h>
#include <memory>
#include <iostream>
#include <string>
#include <thread>
#include <csignal>

// Include all service headers
#include "monitoring.grpc.pb.h"
#include "rabbitmq_consumer.h"
#include "metrics_analyzer.h"
#include "alert_manager.h"
#include "ProvisionServiceImpl.h"
#include "grpc_service_impl.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;

// Monitoring Service Implementation
class MonitoringServiceImpl final : public monitoring::MonitoringService::Service {
private:
    AlertManager* alert_manager_;
    std::mutex registered_devices_mutex_;
    std::map<std::string, grpc::ServerWriter<monitoring::Alert>*> registered_devices_;

public:
    explicit MonitoringServiceImpl(AlertManager* alert_manager)
        : alert_manager_(alert_manager) {}

    Status RegisterDevice(ServerContext* context,
                         const monitoring::DeviceInfo* request,
                         ServerWriter<monitoring::Alert>* writer) override {
        std::string device_id = request->device_id();
        std::cout << "📡 [MONITORING] Registering device: " << device_id << std::endl;

        // Enregistrer le device dans la map
        {
            std::lock_guard<std::mutex> lock(registered_devices_mutex_);
            registered_devices_[device_id] = writer;
        }

        // Enregistrer dans l'AlertManager
        alert_manager_->registerDevice(device_id, writer);

        std::cout << "❌ [MONITORING] Device disconnected: " << device_id << std::endl;
        
        // Nettoyer lors de la déconnexion
        {
            std::lock_guard<std::mutex> lock(registered_devices_mutex_);
            registered_devices_.erase(device_id);
        }
        
        alert_manager_->unregisterDevice(device_id);
        return Status::OK;
    }


private:
    // void SendTestAlert(const std::string& device_id, ServerWriter<monitoring::Alert>* writer) {
    //     std::cout << "[DEBUG] Sending test alert to device: " << device_id << std::endl;
        
    //     monitoring::Alert test_alert;
    //     test_alert.set_alert_type("SYSTEM_TEST");
    //     test_alert.set_severity(monitoring::Alert::INFO);
    //     test_alert.set_description("Test alert - Monitoring service is working!");
    //     test_alert.set_recommended_action("No action needed - this is a test");
    //     test_alert.set_timestamp(std::to_string(
    //         std::chrono::duration_cast<std::chrono::seconds>(
    //             std::chrono::system_clock::now().time_since_epoch()).count()));
    //     test_alert.set_corrective_command("");

    //     try {
    //         if (!writer->Write(test_alert)) {
    //             std::cout << "[ERROR] Failed to send test alert to device: " << device_id << std::endl;
    //         } else {
    //             std::cout << "[SUCCESS] Test alert sent to device: " << device_id << std::endl;
    //         }
    //     } catch (const std::exception& e) {
    //         std::cout << "[ERROR] Exception sending test alert: " << e.what() << std::endl;
    //     }
    // }

    // void CheckAndSendAlerts(const std::string& device_id, ServerWriter<monitoring::Alert>* writer) {
    // static int alert_counter = 0;
    // static auto last_alert_time = std::chrono::steady_clock::now();
    // auto now = std::chrono::steady_clock::now();
    
    // // Envoyer une alerte toutes les 30 secondes pour test (au lieu de 2 minutes)
    // if (std::chrono::duration_cast<std::chrono::seconds>(now - last_alert_time).count() >= 30) {
    //     alert_counter++;
        
    //     std::cout << "[DEBUG] Preparing to send periodic alert #" << alert_counter 
    //               << " to device: " << device_id << std::endl;
        
    //     monitoring::Alert periodic_alert;
    //     periodic_alert.set_alert_type("PERIODIC_CHECK");
    //     periodic_alert.set_severity(monitoring::Alert::WARNING);
    //     periodic_alert.set_description("Periodic system check #" + std::to_string(alert_counter));
    //     periodic_alert.set_recommended_action("Monitor system resources");
    //     periodic_alert.set_timestamp(std::to_string(
    //         std::chrono::duration_cast<std::chrono::seconds>(
    //             std::chrono::system_clock::now().time_since_epoch()).count()));
    //     periodic_alert.set_corrective_command("echo 'System check performed'");

    //     try {
    //         std::cout << "[DEBUG] Attempting to write periodic alert to stream..." << std::endl;
            
    //         if (writer->Write(periodic_alert)) {
    //             std::cout << "[SUCCESS] Periodic alert #" << alert_counter 
    //                       << " sent successfully to device: " << device_id << std::endl;
    //         } else {
    //             std::cout << "[ERROR] Failed to write periodic alert to stream for device: " 
    //                       << device_id << std::endl;
    //         }
    //     } catch (const std::exception& e) {
    //         std::cout << "[ERROR] Exception sending periodic alert: " << e.what() << std::endl;
    //     }
        
    //     last_alert_time = now;
    // }
    // }
};
// Configuration structure for server parameters
struct ServerConfig {
    // RabbitMQ configuration
    std::string rabbitmq_host = "localhost";
    int rabbitmq_port = 5672;
    std::string rabbitmq_username = "guest";
    std::string rabbitmq_password = "guest";
    std::string hw_queue = "hardware_metrics";
    std::string sw_queue = "software_metrics";
    
    // File paths
    std::string ota_updates_path = "/home/manar/IOTSHADOW/ota-update-service/server/updates/app";
    
    // Server configuration
    std::string grpc_address = "0.0.0.0:50051";
};

class UnifiedServer {
private:
    ServerConfig config_;
    std::unique_ptr<AlertManager> alert_manager_;
    std::unique_ptr<MetricsAnalyzer> metrics_analyzer_;
    std::unique_ptr<RabbitMQConsumer> rabbitmq_consumer_;
    std::shared_ptr<DBHandler> db_manager_;
    std::shared_ptr<JWTUtils> jwt_manager_;
    std::unique_ptr<OTAUpdateService> ota_service_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<MonitoringServiceImpl> monitoring_service_;

public:
    explicit UnifiedServer(const ServerConfig& config) : config_(config) {}

    bool Initialize() {
        try {
            std::cout << "⚙️ [SERVER] Initializing unified gRPC server..." << std::endl;

            alert_manager_ = std::make_unique<AlertManager>();
            metrics_analyzer_ = std::make_unique<MetricsAnalyzer>(
                alert_manager_.get() );
            
            rabbitmq_consumer_ = std::make_unique<RabbitMQConsumer>(
                config_.rabbitmq_host, config_.rabbitmq_port,
                config_.rabbitmq_username, config_.rabbitmq_password,
                config_.hw_queue, config_.sw_queue);

            // Améliorer les callbacks pour traiter les métriques
            auto hw_callback = [this](const std::string& device_id, const nlohmann::json& metrics) {
                std::cout << "[DEBUG] Processing HW metrics from device: " << device_id << std::endl;
                
                metrics_analyzer_->processHardwareMetrics(device_id, metrics);
                
                // Générer des alertes basées sur les métriques reçues
                GenerateMetricsBasedAlerts(device_id, metrics, "HARDWARE");
            };

            auto sw_callback = [this](const std::string& device_id, const nlohmann::json& metrics) {
                std::cout << "[DEBUG] Processing SW metrics from device: " << device_id << std::endl;
                
                metrics_analyzer_->processSoftwareMetrics(device_id, metrics);
                
                // Générer des alertes basées sur les métriques reçues
                GenerateMetricsBasedAlerts(device_id, metrics, "SOFTWARE");
            };

            if (!rabbitmq_consumer_->start(hw_callback, sw_callback)) {
                std::cerr << "❌ [ERROR] Failed to start RabbitMQ consumer" << std::endl;
                return false;
            }

            db_manager_ = std::make_shared<DBHandler>();
            jwt_manager_ = std::make_shared<JWTUtils>();

            ota_service_ = std::make_unique<OTAUpdateService>(config_.ota_updates_path);
            if (!ota_service_->InitializeDatabase()) {
                std::cerr << "❌ [ERROR] Failed to initialize OTA database" << std::endl;
                return false;
            }

            std::cout << "✅ [SERVER] All services initialized successfully" << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "🔥 [ERROR] Initialization failed: " << e.what() << std::endl;
            return false;
        }
    }

    void Run() {
        try {
            monitoring_service_ = std::make_unique<MonitoringServiceImpl>(alert_manager_.get());
            ProvisioningServiceImpl provisioning_service(db_manager_, jwt_manager_);
            OTAUpdateServiceImpl ota_service_impl(std::move(ota_service_));

            ServerBuilder builder;
            builder.AddListeningPort(config_.grpc_address, grpc::InsecureServerCredentials());
            builder.RegisterService(monitoring_service_.get());
            builder.RegisterService(&provisioning_service);
            builder.RegisterService(&ota_service_impl);

            server_ = builder.BuildAndStart();
            
            std::cout << "\n🚀 [SERVER] =================================" << std::endl;
            std::cout << "🌐 [SERVER] Unified gRPC Server Started" << std::endl;
            std::cout << "📍 Address: " << config_.grpc_address << std::endl;
            std::cout << "🔒 Connection: secure" << std::endl;
            std::cout << "🧩 Services Available:" << std::endl;
            std::cout << "   - 📡 Monitoring Service" << std::endl;
            std::cout << "   - 🛠️ Provisioning Service" << std::endl;
            std::cout << "   - 📦 OTA Update Service" << std::endl;
            std::cout << "=============================================" << std::endl;

            server_->Wait();

        } catch (const std::exception& e) {
            std::cerr << "🔥 [ERROR] Server runtime error: " << e.what() << std::endl;
        }
    }

    void Shutdown() {
        std::cout << "🔻 [SERVER] Shutting down..." << std::endl;
        
        if (server_) {
            server_->Shutdown();
        }
        
        if (rabbitmq_consumer_) {
            rabbitmq_consumer_->stop();
        }
        
        std::cout << "🛑 [SERVER] Shutdown complete" << std::endl;
    }


private:
    void GenerateMetricsBasedAlerts(const std::string& device_id, 
                               const nlohmann::json& metrics, 
                               const std::string& type) {
    try {
        if (type == "HARDWARE") {
            // CPU Usage Analysis
            if (metrics.contains("cpu_usage")) {
                std::string cpu_usage = metrics["cpu_usage"].get<std::string>();
                metrics_analyzer_->analyzeCpuUsage(device_id, cpu_usage);
            }
            
            // Memory Usage Analysis
            if (metrics.contains("memory_usage")) {
                std::string memory_usage = metrics["memory_usage"].get<std::string>();
                metrics_analyzer_->analyzeMemoryUsage(device_id, memory_usage);
            }
            
            // Disk Usage Analysis
            if (metrics.contains("disk_usage")) {
                std::string disk_usage = metrics["disk_usage"].get<std::string>();
                metrics_analyzer_->analyzeDiskUsage(device_id, disk_usage);
            }
            
            // USB State Analysis
            if (metrics.contains("usb_state")) {
                std::string usb_state = metrics["usb_state"].get<std::string>();
                metrics_analyzer_->analyzeUsbState(device_id, usb_state);
            }
            
            // GPIO State Analysis
            if (metrics.contains("gpio_state")) {
                int current_gpio_state = metrics["gpio_state"].get<int>();
                
                // Get previous GPIO state from device state
                auto device_state = metrics_analyzer_->getDeviceState(device_id);
                int previous_gpio_state = device_state.gpio_state;
                
                metrics_analyzer_->analyzeGpioState(device_id, current_gpio_state, previous_gpio_state);
            }
        }
        
        if (type == "SOFTWARE") {
            // Network Status Analysis
            if (metrics.contains("network_status")) {
                std::string network_status = metrics["network_status"].get<std::string>();
                metrics_analyzer_->analyzeNetworkStatus(device_id, network_status);
            }
            
            // Services Analysis
            if (metrics.contains("services")) {
                std::map<std::string, std::string> services_map;
                auto services = metrics["services"];
                
                // Convert JSON object to map
                for (auto& [service, status] : services.items()) {
                    services_map[service] = status.get<std::string>();
                }
                
                // CORRECTION: Passer le services_map au lieu de rien
                metrics_analyzer_->analyzeServices(device_id, services_map);
            }
        }
        
        std::cout << "[DEBUG] Generated alerts for " << type << " metrics from device: " << device_id << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "[ERROR] Exception generating " << type << " alerts for device " 
                  << device_id << ": " << e.what() << std::endl;
    }
}
};

ServerConfig LoadConfiguration() {
    ServerConfig config;
    return config;
}

int main() {
    try {
        ServerConfig config = LoadConfiguration();
        UnifiedServer server(config);
        
        if (!server.Initialize()) {
            std::cerr << "❌ [ERROR] Failed to initialize server" << std::endl;
            return 1;
        }

        std::signal(SIGINT, [](int signal) {
            std::cout << "\n⚠️ [SERVER] Received shutdown signal (Ctrl+C)" << std::endl;
            exit(0);
        });

        server.Run();
        
    } catch (const std::exception& e) {
        std::cerr << "💥 [ERROR] Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
