// shadow_agent_client/src/main.cpp
#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <openssl/sha.h>
#include <grpcpp/grpcpp.h>

// Include headers for all services
#include "ProvisionClientImpl.h"
#include "ConfigManager.h"
#include "metrics_collector.h"
#include "rabbitmq_sender.h"
#include "ota_service.grpc.pb.h"
#include "monitoring.grpc.pb.h"
#include "provisioning.grpc.pb.h"


using namespace grpc;
using namespace std;

// Global structures for storing alerts and OTA messages
struct AlertMessage {
    string type;
    string severity;
    string description;
    string recommended_action;
    string timestamp;
    string corrective_command;
};

struct OTAMessage {
    string app_name;
    string version;
    string status;
    string timestamp;
    string details;
};

class ShadowAgentClient {
private:
    // Provisioning client
    unique_ptr<ProvisioningClient> provision_client;
    
    // OTA client components
    unique_ptr<ota::OTAUpdateService::Stub> ota_stub;
    
    // Monitoring client components
    unique_ptr<monitoring::MonitoringService::Stub> monitoring_stub;
    unique_ptr<MetricsCollector> metrics_collector;
    unique_ptr<RabbitMQSender> rabbitmq_sender;
    
    // Background threads
    thread ota_thread;
    thread monitoring_thread;
    thread alert_thread;
    
    // Message queues
    queue<AlertMessage> alert_queue;
    queue<OTAMessage> ota_queue;
    mutex alert_mutex;
    mutex ota_mutex;
    
    // Control flags
    atomic<bool> running{false};
    atomic<bool> authenticated{false};
    
    // User session data
    string jwt_token;
    int current_device_id = -1;
    string device_id_str;

public:
    ShadowAgentClient(const string& server_address, const string& rabbitmq_host) {
        // Initialize gRPC channel
        auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
        
        // Initialize clients
        provision_client = make_unique<ProvisioningClient>(channel);
        ota_stub = ota::OTAUpdateService::NewStub(channel);
        monitoring_stub = monitoring::MonitoringService::NewStub(channel);
        
        // Initialize monitoring components
        metrics_collector = make_unique<MetricsCollector>("../../logs");
        rabbitmq_sender = make_unique<RabbitMQSender>(
            rabbitmq_host, 5672, "guest", "guest", "hardware_metrics", "software_metrics");
            
        // Connect to RabbitMQ
        if (!rabbitmq_sender->connect()) {
            cerr << "Warning: Failed to connect to RabbitMQ" << endl;
        }
    }
    
    ~ShadowAgentClient() {
        StopBackgroundServices();
    }
    
    void StartBackgroundServices() {
        if (running) return;
        
        running = true;
        
        // Start OTA update checker
        ota_thread = thread([this]() {
            while (running) {
                CheckOTAUpdates();
                this_thread::sleep_for(chrono::minutes(5));
            }
        });
        
        // Start monitoring service
        monitoring_thread = thread([this]() {
            RegisterMonitoringDevice();
            while (running) {
                CollectAndSendMetrics();
                this_thread::sleep_for(chrono::seconds(60));
            }
        });
        
        cout << "Background services started (OTA updates every 5 minutes, monitoring every minute)" << endl;
    }
    
    void StopBackgroundServices() {
        running = false;
        
        if (ota_thread.joinable()) {
            ota_thread.join();
        }
        if (monitoring_thread.joinable()) {
            monitoring_thread.join();
        }
        if (alert_thread.joinable()) {
            alert_thread.join();
        }
    }
    
    void ShowAuthMenu() {
        cout << "\n=== Shadow Agent - Authentification ===" << endl;
        cout << "1. Se connecter" << endl;
        cout << "2. S'enregistrer" << endl;
        cout << "0. Quitter" << endl;
        cout << "Choix: ";
    }
    
    void ShowMainMenu() {
        cout << "\n=== Shadow Agent - Menu Principal ===" << endl;
        cout << "=== Gestion des Dispositifs ===" << endl;
        cout << "1. Supprimer un dispositif" << endl;
        cout << "2. Mettre à jour un dispositif" << endl;
        cout << "3. Afficher tous les dispositifs" << endl;
        cout << "4. Afficher un dispositif par ID" << endl;
        cout << "=== Monitoring & Updates ===" << endl;
        cout << "5. Voir les alertes de monitoring" << endl;
        cout << "6. Voir les messages OTA" << endl;
        cout << "7. Forcer vérification OTA" << endl;
        cout << "8. Voir statut des services" << endl;
        cout << "=== Session ===" << endl;
        cout << "9. Se déconnecter" << endl;
        cout << "0. Quitter" << endl;
        cout << "Choix: ";
    }
    
    bool AuthenticateUser() {
        string hostname, password;
        
        cout << "\n=== Connexion ===" << endl;
        cout << "Hostname: ";
        getline(cin, hostname);
        cout << "Password: ";
        getline(cin, password);
        
        if (provision_client->Authenticate(hostname, password, jwt_token)) {
            cout << "Connexion réussie!" << endl;
            
            // Try to load device ID from config
            string stored_device_id;
            if (ConfigManager::loadDeviceInfo(hostname, stored_device_id)) {
                current_device_id = stoi(stored_device_id);
                device_id_str = stored_device_id;
            }
            
            authenticated = true;
            StartBackgroundServices();
            return true;
        } else {
            cout << "Échec de la connexion!" << endl;
            return false;
        }
    }
    
    bool RegisterUser() {
        string hostname, password, user, location, hardware_type, os_type;
        
        cout << "\n=== Nouveau Dispositif ===" << endl;
        cout << "Hostname: ";
        getline(cin, hostname);
        cout << "Password: ";
        getline(cin, password);
        cout << "User: ";
        getline(cin, user);
        cout << "Location: ";
        getline(cin, location);
        cout << "Hardware Type: ";
        getline(cin, hardware_type);
        cout << "OS Type: ";
        getline(cin, os_type);
        
        if (provision_client->AddDevice(hostname, password, user, location, hardware_type,
                            os_type, current_device_id, jwt_token)) {
            cout << "Enregistrement réussi! ID: " << current_device_id << endl;
            device_id_str = to_string(current_device_id);
            authenticated = true;
            StartBackgroundServices();
            return true;
        } else {
            cout << "Échec de l'enregistrement!" << endl;
            return false;
        }
    }
    
    void HandleMainMenu() {
        int choice;
        string user, location, hardware_type, os_type;
        int device_id;
        
        while (authenticated) {
            ShowMainMenu();
            cin >> choice;
            cin.ignore();
            
            switch (choice) {
                case 1: {
                    cout << "Device ID to delete: ";
                    cin >> device_id;
                    cin.ignore();
                    provision_client->DeleteDevice(device_id);
                    break;
                }
                
                case 2: {
                    cout << "Device ID to update (current: " << current_device_id << "): ";
                    cin >> device_id;
                    cin.ignore();
                    
                    cout << "User: ";
                    getline(cin, user);
                    cout << "Location: ";
                    getline(cin, location);
                    cout << "Hardware Type: ";
                    getline(cin, hardware_type);
                    cout << "OS Type: ";
                    getline(cin, os_type);
                    
                    provision_client->UpdateDevice(device_id, user, location, hardware_type, os_type);
                    break;
                }
                
                case 3: {
                    provision_client->GetAllDevices();
                    break;
                }
                
                case 4: {
                    cout << "Device ID (current: " << current_device_id << "): ";
                    cin >> device_id;
                    cin.ignore();
                    provision_client->GetDeviceById(device_id);
                    break;
                }
                
                case 5: {
                    ShowMonitoringAlerts();
                    break;
                }
                
                case 6: {
                    ShowOTAMessages();
                    break;
                }
                
                case 7: {
                    cout << "Forcing OTA update check..." << endl;
                    CheckOTAUpdates();
                    break;
                }
                
                case 8: {
                    ShowServiceStatus();
                    break;
                }
                
                case 9: {
                    cout << "Déconnexion en cours..." << endl;
                    authenticated = false;
                    StopBackgroundServices();
                    jwt_token.clear();
                    current_device_id = -1;
                    device_id_str.clear();
                    return;
                }
                
                case 0: {
                    cout << "Arrêt du Shadow Agent..." << endl;
                    StopBackgroundServices();
                    exit(0);
                }
                
                default: {
                    cout << "Choix invalide!" << endl;
                    break;
                }
            }
        }
    }
    
    void ShowMonitoringAlerts() {
        lock_guard<mutex> lock(alert_mutex);
        
        cout << "\n=== Alertes de Monitoring ===" << endl;
        if (alert_queue.empty()) {
            cout << "Aucune alerte récente." << endl;
            return;
        }
        
        queue<AlertMessage> temp_queue = alert_queue; // Copy for display
        int count = 0;
        
        while (!temp_queue.empty() && count < 10) { // Show last 10 alerts
            const AlertMessage& alert = temp_queue.front();
            cout << "\n--- Alerte " << (count + 1) << " ---" << endl;
            cout << "Type: " << alert.type << endl;
            cout << "Sévérité: " << alert.severity << endl;
            cout << "Description: " << alert.description << endl;
            cout << "Action recommandée: " << alert.recommended_action << endl;
            cout << "Timestamp: " << alert.timestamp << endl;
            if (!alert.corrective_command.empty()) {
                cout << "Commande corrective: " << alert.corrective_command << endl;
            }
            temp_queue.pop();
            count++;
        }
        
        cout << "\nAppuyez sur Entrée pour continuer...";
        cin.get();
    }
    
    void ShowOTAMessages() {
        lock_guard<mutex> lock(ota_mutex);
        
        cout << "\n=== Messages OTA ===" << endl;
        if (ota_queue.empty()) {
            cout << "Aucun message OTA récent." << endl;
            return;
        }
        
        queue<OTAMessage> temp_queue = ota_queue; // Copy for display
        int count = 0;
        
        while (!temp_queue.empty() && count < 10) { // Show last 10 messages
            const OTAMessage& msg = temp_queue.front();
            cout << "\n--- Message OTA " << (count + 1) << " ---" << endl;
            cout << "Application: " << msg.app_name << endl;
            cout << "Version: " << msg.version << endl;
            cout << "Statut: " << msg.status << endl;
            cout << "Timestamp: " << msg.timestamp << endl;
            cout << "Détails: " << msg.details << endl;
            temp_queue.pop();
            count++;
        }
        
        cout << "\nAppuyez sur Entrée pour continuer...";
        cin.get();
    }
    
    void ShowServiceStatus() {
        cout << "\n=== Statut des Services ===" << endl;
        cout << "Services d'arrière-plan: " << (running ? "ACTIFS" : "ARRÊTÉS") << endl;
        cout << "Device ID: " << current_device_id << endl;
        cout << "Authentifié: " << (authenticated ? "OUI" : "NON") << endl;
        
        {
            lock_guard<mutex> lock(alert_mutex);
            cout << "Alertes en attente: " << alert_queue.size() << endl;
        }
        
        {
            lock_guard<mutex> lock(ota_mutex);
            cout << "Messages OTA en attente: " << ota_queue.size() << endl;
        }
        
        cout << "\nAppuyez sur Entrée pour continuer...";
        cin.get();
    }
    
    void Run() {
        cout << "\n=== Shadow Agent - Système Unifié ===" << endl;
        cout << "Gestion des dispositifs, monitoring et mises à jour OTA" << endl;
        
        while (true) {
            if (!authenticated) {
                ShowAuthMenu();
                int choice;
                cin >> choice;
                cin.ignore();
                
                switch (choice) {
                    case 1: {
                        AuthenticateUser();
                        break;
                    }
                    
                    case 2: {
                        RegisterUser();
                        break;
                    }
                    
                    case 0: {
                        cout << "Au revoir!" << endl;
                        StopBackgroundServices();
                        return;
                    }
                    
                    default: {
                        cout << "Choix invalide!" << endl;
                        break;
                    }
                }
            } else {
                HandleMainMenu();
            }
        }
    }

private:
    void CheckOTAUpdates() {
        if (!authenticated) return;
        
        try {
            for (const auto& entry : filesystem::directory_iterator("/opt")) {
                if (entry.is_regular_file()) {
                    string filename = entry.path().filename().string();
                    string app_name = filename;
                    string version = "0";
                    size_t last_underscore = filename.rfind('_');
                    if (last_underscore != string::npos && last_underscore + 1 < filename.size()) {
                        app_name = filename.substr(0, last_underscore);
                        version = filename.substr(last_underscore + 1);
                    }

                    ota::CheckUpdatesRequest request;
                    request.set_device_id(current_device_id);
                    request.set_app_name(app_name);
                    request.set_current_version(version);

                    ota::CheckUpdatesResponse response;
                    grpc::ClientContext context;
                    grpc::Status status = ota_stub->CheckForUpdates(&context, request, &response);

                    if (!status.ok()) {
                        AddOTAMessage(app_name, version, "ERROR", 
                                    "Failed to check updates: " + status.error_message());
                        continue;
                    }

                    if (!response.has_updates()) {
                        AddOTAMessage(app_name, version, "UP_TO_DATE", "No updates available");
                        continue;
                    }

                    for (const auto& update : response.available_updates()) {
                        AddOTAMessage(update.app_name(), update.version(), "AVAILABLE", 
                                    "New update found");
                        
                        if (DownloadAndApplyUpdate(update)) {
                            AddOTAMessage(update.app_name(), update.version(), "SUCCESS", 
                                        "Update applied successfully");
                        } else {
                            AddOTAMessage(update.app_name(), update.version(), "FAILED", 
                                        "Failed to apply update");
                        }
                    }
                }
            }
        } catch (const exception& e) {
            AddOTAMessage("SYSTEM", "N/A", "ERROR", 
                        "Exception in OTA check: " + string(e.what()));
        }
    }
    
    bool DownloadAndApplyUpdate(const ota::UpdateInfo& update) {
        // Implementation similar to the original OTA client
        try {
            ota::DownloadRequest dl_request;
            dl_request.set_device_id(current_device_id);
            dl_request.set_app_name(update.app_name());

            grpc::ClientContext context;
            auto reader = ota_stub->DownloadUpdate(&context, dl_request);

            vector<char> file_data;
            ota::DownloadResponse chunk;

            while (reader->Read(&chunk)) {
                const string& data = chunk.data();
                file_data.insert(file_data.end(), data.begin(), data.end());
            }

            grpc::Status status = reader->Finish();
            if (!status.ok()) {
                return false;
            }

            string calculated_checksum = CalculateChecksum(file_data);
            if (calculated_checksum != update.checksum()) {
                return false;
            }

            return ApplyUpdate(update, file_data);

        } catch (const exception& e) {
            return false;
        }
    }
    
    bool ApplyUpdate(const ota::UpdateInfo& update, const vector<char>& data) {
        try {
            string target_path = "/opt/" + update.app_name() + "_" + update.version();
            
            ofstream outfile(target_path, ios::binary);
            if (!outfile) {
                return false;
            }
            outfile.write(data.data(), data.size());
            outfile.close();

            string chmod_cmd = "chmod +x " + target_path;
            system(chmod_cmd.c_str());

            return true;
        } catch (const exception& e) {
            return false;
        }
    }
    
    void RegisterMonitoringDevice() {
        if (!authenticated) return;
        
        monitoring::DeviceInfo device_info;
        device_info.set_device_id(device_id_str);

        grpc::ClientContext* context = new grpc::ClientContext();
        auto reader = monitoring_stub->RegisterDevice(context, device_info);

        alert_thread = thread([this, reader = move(reader), context]() {
            monitoring::Alert alert;
            while (reader->Read(&alert)) {
                ProcessAlert(alert);
            }
            grpc::Status status = reader->Finish();
            delete context;
        });
    }
    
    void CollectAndSendMetrics() {
        if (!authenticated) return;
        
        try {
            auto [hw_file, sw_file] = metrics_collector->collectMetrics();
            auto hw_metrics = metrics_collector->parseHardwareMetrics(hw_file);
            auto sw_metrics = metrics_collector->parseSoftwareMetrics(sw_file);
            
            rabbitmq_sender->sendHardwareMetrics(hw_metrics);
            rabbitmq_sender->sendSoftwareMetrics(sw_metrics);
            
        } catch (const exception& e) {
            // Log error silently
        }
    }
    
    void ProcessAlert(const monitoring::Alert& alert) {
        AlertMessage msg;
        msg.type = alert.alert_type();
        msg.severity = monitoring::Alert::Severity_Name(alert.severity());
        msg.description = alert.description();
        msg.recommended_action = alert.recommended_action();
        msg.timestamp = alert.timestamp();
        msg.corrective_command = alert.corrective_command();
        
        AddAlertMessage(msg);
        
        // Execute corrective command if present
        if (!alert.corrective_command().empty()) {
            ExecuteCorrectiveCommand(alert.corrective_command());
        }
    }
    
    void ExecuteCorrectiveCommand(const string& cmds) {
        istringstream iss(cmds);
        string cmd;
        while (getline(iss, cmd, ';')) {
            if (!cmd.empty()) {
                system(cmd.c_str());
            }
        }
    }
    
    void AddAlertMessage(const AlertMessage& alert) {
        lock_guard<mutex> lock(alert_mutex);
        alert_queue.push(alert);
        
        // Keep only last 50 alerts
        while (alert_queue.size() > 50) {
            alert_queue.pop();
        }
    }
    
    void AddOTAMessage(const string& app_name, const string& version, 
                      const string& status, const string& details) {
        lock_guard<mutex> lock(ota_mutex);
        
        OTAMessage msg;
        msg.app_name = app_name;
        msg.version = version;
        msg.status = status;
        msg.details = details;
        msg.timestamp = to_string(chrono::duration_cast<chrono::seconds>(
            chrono::system_clock::now().time_since_epoch()).count());
        
        ota_queue.push(msg);
        
        // Keep only last 50 messages
        while (ota_queue.size() > 50) {
            ota_queue.pop();
        }
    }
    
    string CalculateChecksum(const vector<char>& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, data.data(), data.size());
        SHA256_Final(hash, &sha256);
        
        stringstream ss;
        for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << hex << setw(2) << setfill('0') << (int)hash[i];
        }
        return ss.str();
    }
};

int main(int argc, char** argv) {
    // Adresse du serveur à initialiser 
    std::string Adresse_server = "127.0.0.1"; 
    // Utilisation pour gRPC et RabbitMQ
    std::string grpc_address = Adresse_server + ":50051";
    std::string rabbitmq_host = Adresse_server;

    ShadowAgentClient client(grpc_address, rabbitmq_host);
    client.Run();

    return 0;
}