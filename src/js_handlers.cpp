/**
 * JavaScript Handlers - Implementation
 */

#include "js_handlers.hpp"
#include "json_utils.hpp"
#include "rdp_launcher.hpp"
#include "config_manager.hpp"
#include "dialog_manager.hpp"
#include "gui/aad_auth_handler.hpp"
#include "rdp_file_parser.hpp"

#include <iostream>
#include <jansson.h>

// Static instance pointer
JSHandlers* JSHandlers::s_instance = nullptr;

JSHandlers::JSHandlers(RDPLauncher& rdp_launcher,
                       ConfigManager& config_manager,
                       DialogManager& dialog_manager,
                       AADAuthHandler& aad_auth_handler)
    : m_rdp_launcher(rdp_launcher)
    , m_config_manager(config_manager)
    , m_dialog_manager(dialog_manager)
    , m_aad_auth_handler(aad_auth_handler)
{
    s_instance = this;
}

JSHandlers::~JSHandlers() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void JSHandlers::bind_all(webui::window& window) {
    window.bind("connectRDP", s_connect_rdp);
    window.bind("getConnections", s_get_connections);
    window.bind("saveConnection", s_save_connection);
    window.bind("deleteConnection", s_delete_connection);
    window.bind("getAppInfo", s_get_app_info);
    window.bind("importRdpFile", s_import_rdp_file);
    
    // Dialog response handlers
    window.bind("certificateResponse", s_certificate_response);
    window.bind("authResponse", s_auth_response);
    window.bind("aadAuthResponse", s_aad_auth_response);
}

// ============================================================================
// Static callback wrappers
// ============================================================================

void JSHandlers::s_connect_rdp(webui::window::event* e) {
    if (!s_instance) return;
    
    try {
        // Get JSON string from JavaScript
        std::string json_str = e->get_string(0);
        
        std::cout << "[RDPMAN] Received connection request" << std::endl;
        
        if (json_str.empty()) {
            e->return_string("{\"success\": false, \"error\": \"Empty connection parameters\"}");
            return;
        }
        
        // Parse JSON
        json_error_t error;
        json_t* root = json_loads(json_str.c_str(), 0, &error);
        if (!root) {
            std::cerr << "[RDPMAN] JSON parse error: " << error.text << std::endl;
            e->return_string("{\"success\": false, \"error\": \"Invalid JSON: " + json_utils::escape_string(error.text) + "\"}");
            return;
        }
        
        // Extract parameters from JSON
        std::string host = json_utils::get_string(root, "hostname");
        int port = json_utils::get_int(root, "port", 3389);
        std::string username = json_utils::get_string(root, "username");
        std::string domain = json_utils::get_string(root, "domain");
        
        // Advanced options
        bool home_drive = json_utils::get_bool(root, "home_drive");
        bool clipboard = json_utils::get_bool(root, "clipboard", true);
        bool cert_tofu = json_utils::get_bool(root, "cert_tofu");
        bool usb_auto = json_utils::get_bool(root, "usb_auto");
        bool floatbar = json_utils::get_bool(root, "floatbar");
        bool dynamic_resolution = json_utils::get_bool(root, "dynamic_resolution");
        bool network_auto = json_utils::get_bool(root, "network_auto");
        bool gfx_avc420 = json_utils::get_bool(root, "gfx_avc420");
        bool compression = json_utils::get_bool(root, "compression");
        bool audio_pulse = json_utils::get_bool(root, "audio_pulse");
        bool prevent_session_lock = json_utils::get_bool(root, "prevent_session_lock");
        bool auto_reconnect = json_utils::get_bool(root, "auto_reconnect");
        int auto_reconnect_max_retries = json_utils::get_int(root, "auto_reconnect_max_retries", 3);
        
        // Gateway / AVD options
        std::string gateway_hostname = json_utils::get_string(root, "gateway_hostname");
        bool enable_rds_aad_auth = json_utils::get_bool(root, "enable_rds_aad_auth");
        bool target_is_aad_joined = json_utils::get_bool(root, "target_is_aad_joined");
        std::string load_balance_info = json_utils::get_string(root, "load_balance_info");
        
        // Additional AVD parameters
        std::string aad_tenant_id = json_utils::get_string(root, "aad_tenant_id");
        std::string wvd_endpoint_pool = json_utils::get_string(root, "wvd_endpoint_pool");
        std::string arm_path = json_utils::get_string(root, "arm_path");
        std::string workspace_id = json_utils::get_string(root, "workspace_id");
        std::string remote_application_program = json_utils::get_string(root, "remote_application_program");
        
        // Free the JSON object
        json_decref(root);
        
        std::cout << "[RDPMAN] Initiating RDP connection to " << host << ":" << port << std::endl;
    
        if (host.empty()) {
            e->return_string("{\"success\": false, \"error\": \"Host cannot be empty\"}");
            return;
        }
    
        RDPConnectionParams params;
        params.hostname = host;
        params.port = (port > 0) ? port : 3389;
        params.username = username;
        params.domain = domain;
        params.width = 1920;
        params.height = 1080;
        params.fullscreen = false;
        
        // Advanced features
        params.home_drive = home_drive;
        params.clipboard = clipboard;
        params.cert_tofu = cert_tofu;
        params.usb_auto = usb_auto;
        params.floatbar = floatbar;
        params.dynamic_resolution = dynamic_resolution;
        params.network_auto = network_auto;
        params.gfx_avc420 = gfx_avc420;
        params.compression = compression;
        params.audio_pulse = audio_pulse;
        params.prevent_session_lock = prevent_session_lock;
        params.auto_reconnect = auto_reconnect;
        params.auto_reconnect_max_retries = (auto_reconnect_max_retries > 0) ? auto_reconnect_max_retries : 3;
        
        // Gateway / AVD settings
        if (!gateway_hostname.empty()) {
            // Parse gateway hostname (may include port like "gateway.example.com:443")
            size_t colon_pos = gateway_hostname.rfind(':');
            if (colon_pos != std::string::npos && colon_pos > 0) {
                params.gateway_hostname = gateway_hostname.substr(0, colon_pos);
                try {
                    params.gateway_port = std::stoi(gateway_hostname.substr(colon_pos + 1));
                } catch (...) {
                    params.gateway_port = 443;
                }
            } else {
                params.gateway_hostname = gateway_hostname;
                params.gateway_port = 443;
            }
            params.gateway_usage_method = 1;  // Always use gateway
        }
        
        params.enable_rds_aad_auth = enable_rds_aad_auth;
        params.target_is_aad_joined = target_is_aad_joined;
        params.load_balance_info = load_balance_info;
        
        // Additional AVD fields
        params.aad_tenant_id = aad_tenant_id;
        params.wvd_endpoint_pool = wvd_endpoint_pool;
        params.arm_path = arm_path;
        params.workspace_id = workspace_id;
        params.remote_application_program = remote_application_program;
        
        // Log AVD connection info
        if (params.is_avd_connection()) {
            std::cout << "[RDPMAN] AVD/Dev Box connection detected" << std::endl;
            std::cout << "[RDPMAN]   Gateway: " << params.gateway_hostname << ":" << params.gateway_port << std::endl;
            std::cout << "[RDPMAN]   AAD Auth: " << (params.enable_rds_aad_auth ? "enabled" : "disabled") << std::endl;
            if (!params.aad_tenant_id.empty()) {
                std::cout << "[RDPMAN]   AAD Tenant: " << params.aad_tenant_id << std::endl;
            }
            if (!params.load_balance_info.empty()) {
                std::cout << "[RDPMAN]   Load Balance: " << params.load_balance_info << std::endl;
            }
        }
        
        // Legacy audio setting (keep for compatibility)
        params.audio = true;
        
        bool success = s_instance->m_rdp_launcher.launch(params);
        
        if (success) {
            e->return_string("{\"success\": true}");
        } else {
            std::string error = s_instance->m_rdp_launcher.get_last_error();
            e->return_string("{\"success\": false, \"error\": \"" + json_utils::escape_string(error) + "\"}");
        }
    } catch (const std::exception& ex) {
        std::cerr << "[RDPMAN] Exception in connect_rdp: " << ex.what() << std::endl;
        e->return_string("{\"success\": false, \"error\": \"Internal error: " + json_utils::escape_string(ex.what()) + "\"}");
    } catch (...) {
        std::cerr << "[RDPMAN] Unknown exception in connect_rdp" << std::endl;
        e->return_string("{\"success\": false, \"error\": \"Unknown internal error\"}");
    }
}

void JSHandlers::s_get_connections(webui::window::event* e) {
    if (!s_instance) return;
    
    std::cout << "[RDPMAN] get_connections called" << std::endl;
    std::string json = s_instance->m_config_manager.get_connections_json();
    std::cout << "[RDPMAN] Returning " << json.size() << " bytes of JSON" << std::endl;
    e->return_string(json);
}

void JSHandlers::s_save_connection(webui::window::event* e) {
    if (!s_instance) return;
    
    try {
        // Get JSON string from JavaScript
        std::string json_str = e->get_string(0);
        
        std::cout << "[RDPMAN] save_connection called with JSON: " << json_str.substr(0, 100) << "..." << std::endl;
        
        if (json_str.empty()) {
            std::cerr << "[RDPMAN] save_connection: Empty JSON" << std::endl;
            e->return_bool(false);
            return;
        }
        
        // Parse JSON
        json_error_t error;
        json_t* root = json_loads(json_str.c_str(), 0, &error);
        if (!root) {
            std::cerr << "[RDPMAN] save_connection JSON parse error: " << error.text << std::endl;
            e->return_bool(false);
            return;
        }
        
        // Build ConnectionProfile from JSON
        ConnectionProfile profile;
        profile.name = json_utils::get_string(root, "name");
        profile.hostname = json_utils::get_string(root, "hostname");
        profile.port = json_utils::get_int(root, "port", 3389);
        profile.username = json_utils::get_string(root, "username");
        profile.domain = json_utils::get_string(root, "domain");
        
        std::cout << "[RDPMAN] save_connection: name='" << profile.name << "' host='" << profile.hostname << "' port=" << profile.port << std::endl;
        
        // Advanced options
        profile.home_drive = json_utils::get_bool(root, "home_drive");
        profile.clipboard = json_utils::get_bool(root, "clipboard", true);
        profile.cert_tofu = json_utils::get_bool(root, "cert_tofu");
        profile.usb_auto = json_utils::get_bool(root, "usb_auto");
        profile.floatbar = json_utils::get_bool(root, "floatbar");
        profile.dynamic_resolution = json_utils::get_bool(root, "dynamic_resolution");
        profile.network_auto = json_utils::get_bool(root, "network_auto");
        profile.gfx_avc420 = json_utils::get_bool(root, "gfx_avc420");
        profile.compression = json_utils::get_bool(root, "compression");
        profile.audio_pulse = json_utils::get_bool(root, "audio_pulse");
        profile.prevent_session_lock = json_utils::get_bool(root, "prevent_session_lock");
        profile.auto_reconnect = json_utils::get_bool(root, "auto_reconnect");
        profile.auto_reconnect_max_retries = json_utils::get_int(root, "auto_reconnect_max_retries", 3);
        if (profile.auto_reconnect_max_retries <= 0) {
            profile.auto_reconnect_max_retries = 3;
        }
        
        // Free the JSON object
        json_decref(root);
        
        bool success = s_instance->m_config_manager.save_connection(profile);
        
        std::cout << "[RDPMAN] save_connection result: " << (success ? "success" : "failed") << std::endl;
        e->return_bool(success);
    } catch (const std::exception& ex) {
        std::cerr << "[RDPMAN] Exception in save_connection: " << ex.what() << std::endl;
        e->return_bool(false);
    } catch (...) {
        std::cerr << "[RDPMAN] Unknown exception in save_connection" << std::endl;
        e->return_bool(false);
    }
}

void JSHandlers::s_delete_connection(webui::window::event* e) {
    if (!s_instance) return;
    
    std::string name = e->get_string(0);
    bool success = s_instance->m_config_manager.delete_connection(name);
    e->return_bool(success);
}

void JSHandlers::s_get_app_info(webui::window::event* e) {
    if (!s_instance) return;
    
    std::string info = R"({
        "name": "WebUI RDP Client",
        "version": "0.1.0",
        "freerdp_version": ")" + s_instance->m_rdp_launcher.get_freerdp_version() + R"("
    })";
    e->return_string(info);
}

void JSHandlers::s_import_rdp_file(webui::window::event* e) {
    if (!s_instance) return;
    
    std::string content = e->get_string(0);
    
    std::cout << "[RDPMAN] Importing RDP file content (" << content.size() << " bytes)" << std::endl;
    
    if (content.empty()) {
        e->return_string("{\"success\": false, \"error\": \"Empty file content\"}");
        return;
    }
    
    auto result = RDPFileParser::parse_content(content);
    
    if (!result.has_value()) {
        e->return_string("{\"success\": false, \"error\": \"Failed to parse RDP file\"}");
        return;
    }
    
    std::string json = "{\"success\": true, \"data\": " + RDPFileParser::to_json(result.value()) + "}";
    e->return_string(json);
}

void JSHandlers::s_certificate_response(webui::window::event* e) {
    if (!s_instance) return;
    
    int choice = e->get_int(0);  // 0=reject, 1=accept permanent, 2=accept temporary
    s_instance->m_dialog_manager.on_certificate_response(choice);
}

void JSHandlers::s_auth_response(webui::window::event* e) {
    if (!s_instance) return;
    
    bool success = e->get_bool(0);
    std::string username = e->get_string(1);
    std::string password = e->get_string(2);
    std::string domain = e->get_string(3);
    s_instance->m_dialog_manager.on_auth_response(success, username, password, domain);
}

void JSHandlers::s_aad_auth_response(webui::window::event* e) {
    if (!s_instance) return;
    
    bool success = e->get_bool(0);
    std::string redirect_url = e->get_string(1);
    s_instance->m_aad_auth_handler.on_response(success, redirect_url);
}
