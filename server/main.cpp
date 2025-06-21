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

        // Envoyer une alerte de test immédiatement
        SendTestAlert(device_id, writer);

        // Garder la connexion active
        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Vérifier périodiquement s'il y a des alertes à envoyer
            CheckAndSendAlerts(device_id, writer);
        }

        std::cout << "❌ [MONITORING] Device disconnected: " << device_id << std::endl;
        
        // Nettoyer lors de la déconnexion
        {
            std::lock_guard<std::mutex> lock(registered_devices_mutex_);
            registered_devices_.erase(device_id);
        }
        
        alert_manager_->unregisterDevice(device_id);
        return Status::OK;
    }

    Status SendStatusUpdate(ServerContext* context,
                           const monitoring::StatusUpdate* request,
                           monitoring::StatusResponse* response) override {
        std::string device_id = request->device_id();
        std::string message = request->message();

        std::cout << "📝 [MONITORING] Status update from device " << device_id 
                  << ": " << message << std::endl;

        response->set_success(true);
        response->set_message("Status update received ✅");

        return Status::OK;
    }

private:
    void SendTestAlert(const std::string& device_id, ServerWriter<monitoring::Alert>* writer) {
        std::cout << "[DEBUG] Sending test alert to device: " << device_id << std::endl;
        
        monitoring::Alert test_alert;
        test_alert.set_alert_type("SYSTEM_TEST");
        test_alert.set_severity(monitoring::Alert::INFO);
        test_alert.set_description("Test alert - Monitoring service is working!");
        test_alert.set_recommended_action("No action needed - this is a test");
        test_alert.set_timestamp(std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));
        test_alert.set_corrective_command("");

        try {
            if (!writer->Write(test_alert)) {
                std::cout << "[ERROR] Failed to send test alert to device: " << device_id << std::endl;
            } else {
                std::cout << "[SUCCESS] Test alert sent to device: " << device_id << std::endl;
            }
        } catch (const std::exception& e) {
            std::cout << "[ERROR] Exception sending test alert: " << e.what() << std::endl;
        }
    }

    void CheckAndSendAlerts(const std::string& device_id, ServerWriter<monitoring::Alert>* writer) {
        // Cette méthode peut être utilisée pour envoyer des alertes périodiques
        // ou des alertes basées sur les métriques reçues
        
        static int alert_counter = 0;
        static auto last_alert_time = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        
        // Envoyer une alerte toutes les 2 minutes pour test
        if (std::chrono::duration_cast<std::chrono::minutes>(now - last_alert_time).count() >= 2) {
            alert_counter++;
            
            monitoring::Alert periodic_alert;
            periodic_alert.set_alert_type("PERIODIC_CHECK");
            periodic_alert.set_severity(monitoring::Alert::WARNING);
            periodic_alert.set_description("Periodic system check #" + std::to_string(alert_counter));
            periodic_alert.set_recommended_action("Monitor system resources");
            periodic_alert.set_timestamp(std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()));
            periodic_alert.set_corrective_command("echo 'System check performed'");

            try {
                if (writer->Write(periodic_alert)) {
                    std::cout << "[SUCCESS] Periodic alert #" << alert_counter 
                              << " sent to device: " << device_id << std::endl;
                } else {
                    std::cout << "[ERROR] Failed to send periodic alert to device: " << device_id << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "[ERROR] Exception sending periodic alert: " << e.what() << std::endl;
            }
            
            last_alert_time = now;
        }
    }
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
    std::string thresholds_path = "thresholds.json";
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
                alert_manager_.get(), config_.thresholds_path);
            
            rabbitmq_consumer_ = std::make_unique<RabbitMQConsumer>(
                config_.rabbitmq_host, config_.rabbitmq_port,
                config_.rabbitmq_username, config_.rabbitmq_password,
                config_.hw_queue, config_.sw_queue);

            // Améliorer les callbacks pour traiter les métriques
            auto hw_callback = [this](const std::string& device_id, const nlohmann::json& metrics) {
                std::cout << "[DEBUG] Processing HW metrics from device: " << device_id << std::endl;
                std::cout << "[DEBUG] HW Metrics: " << metrics.dump(2) << std::endl;
                
                metrics_analyzer_->processHardwareMetrics(device_id, metrics);
                
                // Générer des alertes basées sur les métriques reçues
                GenerateMetricsBasedAlerts(device_id, metrics, "HARDWARE");
            };

            auto sw_callback = [this](const std::string& device_id, const nlohmann::json& metrics) {
                std::cout << "[DEBUG] Processing SW metrics from device: " << device_id << std::endl;
                std::cout << "[DEBUG] SW Metrics: " << metrics.dump(2) << std::endl;
                
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
            // Exemple d'alertes basées sur les métriques
            if (type == "HARDWARE") {
                if (metrics.contains("cpu_usage")) {
                    double cpu_usage = metrics["cpu_usage"];
                    if (cpu_usage > 80.0) {
                        SendAlert(device_id, "HIGH_CPU_USAGE", AlertManager::AlertSeverity::WARNING,
                                 "CPU usage is high: " + std::to_string(cpu_usage) + "%",
                                 "Consider stopping unnecessary processes",
                                 "ps aux | sort -nrk 3,3 | head -5");
                    }
                }
                
                if (metrics.contains("memory_usage")) {
                    double memory_usage = metrics["memory_usage"];
                    if (memory_usage > 85.0) {
                        SendAlert(device_id, "HIGH_MEMORY_USAGE", AlertManager::AlertSeverity::CRITICAL,
                                 "Memory usage is critical: " + std::to_string(memory_usage) + "%",
                                 "Free up memory or restart services",
                                 "free -h; sync; echo 3 > /proc/sys/vm/drop_caches");
                    }
                }
            }
            
            if (type == "SOFTWARE") {
                if (metrics.contains("disk_usage")) {
                    double disk_usage = metrics["disk_usage"];
                    if (disk_usage > 90.0) {
                        SendAlert(device_id, "HIGH_DISK_USAGE", AlertManager::AlertSeverity::CRITICAL,
                                 "Disk usage is critical: " + std::to_string(disk_usage) + "%",
                                 "Clean up disk space immediately",
                                 "df -h; find /tmp -type f -atime +7 -delete");
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cout << "[ERROR] Exception generating alerts: " << e.what() << std::endl;
        }
    }

    void SendAlert(const std::string& device_id, const std::string& alert_type,
                   AlertManager::AlertSeverity severity, const std::string& description,
                   const std::string& recommended_action, const std::string& corrective_command) {
        if (alert_manager_) {
            alert_manager_->sendAlert(device_id, severity, alert_type , description,
                                    recommended_action, corrective_command);
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
