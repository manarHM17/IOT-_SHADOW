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
public:
    explicit MonitoringServiceImpl(AlertManager* alert_manager)
        : alert_manager_(alert_manager) {}

    Status RegisterDevice(ServerContext* context,
                         const monitoring::DeviceInfo* request,
                         ServerWriter<monitoring::Alert>* writer) override {
        std::string device_id = request->device_id();
        std::cout << "📡 [MONITORING] Registering device: " << device_id << std::endl;

        alert_manager_->registerDevice(device_id, writer);

        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "❌ [MONITORING] Device disconnected: " << device_id << std::endl;
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
    AlertManager* alert_manager_;
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

            auto hw_callback = [this](const std::string& device_id, const nlohmann::json& metrics) {
                metrics_analyzer_->processHardwareMetrics(device_id, metrics);
            };

            auto sw_callback = [this](const std::string& device_id, const nlohmann::json& metrics) {
                metrics_analyzer_->processSoftwareMetrics(device_id, metrics);
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
            MonitoringServiceImpl monitoring_service(alert_manager_.get());
            ProvisioningServiceImpl provisioning_service(db_manager_, jwt_manager_);
            OTAUpdateServiceImpl ota_service_impl(std::move(ota_service_));

            ServerBuilder builder;
            builder.AddListeningPort(config_.grpc_address, grpc::InsecureServerCredentials());
            builder.RegisterService(&monitoring_service);
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
