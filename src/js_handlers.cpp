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
#include "feed_discovery.hpp"
#include "logger.hpp"

#include <iostream>
#include <jansson.h>
#include <cstdio>
#include <array>
#include <random>
#include <format>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace {

std::string trim_newlines(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string run_command_get_stdout(const char* command) {
    if (!command || command[0] == '\0') {
        return "";
    }

    std::array<char, 512> buffer{};
    std::string output;
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (status != 0) {
        return "";
    }

    return trim_newlines(output);
}

std::string pick_database_file_path() {
#ifdef _WIN32
    char file_buffer[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file_buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Database Files\0*.db;*.sqlite;*.sqlite3\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(file_buffer);
    }
    return "";
#elif defined(__APPLE__)
    return run_command_get_stdout("osascript -e 'POSIX path of (choose file with prompt \"Open Database\")'");
#else
    std::string path = run_command_get_stdout(
        "zenity --file-selection --title='Open Database' "
        "--file-filter='Database files | *.db *.sqlite *.sqlite3' "
        "--file-filter='All files | *'");
    if (!path.empty()) {
        return path;
    }

    path = run_command_get_stdout(
        "kdialog --getopenfilename ~ '*.db *.sqlite *.sqlite3|Database files (*.db *.sqlite *.sqlite3)' 2>/dev/null");
    return trim_newlines(path);
#endif
}

std::string pick_database_save_path() {
#ifdef _WIN32
    char file_buffer[MAX_PATH] = "connections.db";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file_buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Database Files\0*.db;*.sqlite;*.sqlite3\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "db";

    if (GetSaveFileNameA(&ofn) == TRUE) {
        return std::string(file_buffer);
    }
    return "";
#elif defined(__APPLE__)
    return run_command_get_stdout("osascript -e 'POSIX path of (choose file name with prompt \"Create Database\" default name \"connections.db\")'");
#else
    std::string path = run_command_get_stdout(
        "zenity --file-selection --save --confirm-overwrite "
        "--title='Create Database' --filename='connections.db'");
    if (!path.empty()) {
        return path;
    }

    path = run_command_get_stdout(
        "kdialog --getsavefilename ~ 'connections.db|Database files (*.db *.sqlite *.sqlite3)' 2>/dev/null");
    return trim_newlines(path);
#endif
}

}

// Static instance pointer
JSHandlers* JSHandlers::s_instance = nullptr;

JSHandlers::JSHandlers(RDPLauncher& rdp_launcher,
                       ConfigManager& config_manager,
                       DialogManager& dialog_manager,
                       AADAuthHandler& aad_auth_handler,
                       FeedDiscoveryManager& feed_discovery)
    : m_rdp_launcher(rdp_launcher)
    , m_config_manager(config_manager)
    , m_dialog_manager(dialog_manager)
    , m_aad_auth_handler(aad_auth_handler)
    , m_feed_discovery(feed_discovery)
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
    window.bind("createDatabase", s_create_database);
    window.bind("createDatabaseDialog", s_create_database_dialog);
    window.bind("openDatabase", s_open_database);
    window.bind("openDatabaseDialog", s_open_database_dialog);
    window.bind("cloneDatabase", s_clone_database);
    window.bind("closeDatabase", s_close_database);
    window.bind("getDatabaseStatus", s_get_database_status);
    window.bind("createFolder", s_create_folder);
    window.bind("moveFolder", s_move_folder);
    window.bind("renameFolder", s_rename_folder);
    window.bind("deleteFolder", s_delete_folder);
    window.bind("getFolders", s_get_folders);
    
    // Dialog response handlers
    window.bind("certificateResponse", s_certificate_response);
    window.bind("authResponse", s_auth_response);
    window.bind("aadAuthResponse", s_aad_auth_response);

    // Feed discovery handlers
    window.bind("getFeedAccounts", s_get_feed_accounts);
    window.bind("deleteFeedAccount", s_delete_feed_account);
    window.bind("discoverFeeds", s_discover_feeds);
    window.bind("logOffAccount", s_log_off_account);
    window.bind("forgetAccount", s_forget_account);

    // Folder settings (parameter inheritance)
    window.bind("getFolderSettings", s_get_folder_settings);
    window.bind("saveFolderSettings", s_save_folder_settings);
    window.bind("getEffectiveFolderSettings", s_get_effective_folder_settings);
    window.bind("getEffectiveConnectionProfile", s_get_effective_connection_profile);
}

// ============================================================================
// Static callback wrappers
// ============================================================================

void JSHandlers::s_connect_rdp(webui::window::event* e) {
    if (!s_instance) return;
    
    try {
        // Get JSON string from JavaScript
        std::string json_str = e->get_string(0);
        
        LOG_DEBUG("RDPMAN", "Received connection request");
        
        if (json_str.empty()) {
            e->return_string("{\"success\": false, \"error\": \"Empty connection parameters\"}");
            return;
        }
        
        // Parse JSON
        json_error_t error;
        json_utils::JsonPtr root{json_loads(json_str.c_str(), 0, &error)};
        if (!root) {
            LOG_ERROR("RDPMAN", "JSON parse error: " << error.text);
            e->return_string("{\"success\": false, \"error\": \"Invalid JSON: " + json_utils::escape_string(error.text) + "\"}");
            return;
        }
        
        // Extract parameters from JSON
        std::string host = json_utils::get_string(root.get(), "hostname");
        int port = json_utils::get_int(root.get(), "port", 3389);
        std::string username = json_utils::get_string(root.get(), "username");
        std::string domain = json_utils::get_string(root.get(), "domain");
        
        // Advanced options
        bool home_drive = json_utils::get_bool(root.get(), "home_drive");
        bool clipboard = json_utils::get_bool(root.get(), "clipboard", true);
        bool cert_tofu = json_utils::get_bool(root.get(), "cert_tofu");
        bool usb_auto = json_utils::get_bool(root.get(), "usb_auto");
        bool floatbar = json_utils::get_bool(root.get(), "floatbar");
        bool dynamic_resolution = json_utils::get_bool(root.get(), "dynamic_resolution");
        bool network_auto = json_utils::get_bool(root.get(), "network_auto");
        bool gfx_avc420 = json_utils::get_bool(root.get(), "gfx_avc420");
        bool compression = json_utils::get_bool(root.get(), "compression");
        bool audio_pulse = json_utils::get_bool(root.get(), "audio_pulse");
        bool prevent_session_lock = json_utils::get_bool(root.get(), "prevent_session_lock");
        bool auto_reconnect = json_utils::get_bool(root.get(), "auto_reconnect");
        int auto_reconnect_max_retries = json_utils::get_int(root.get(), "auto_reconnect_max_retries", 3);
        
        // Gateway / AVD options
        std::string gateway_hostname = json_utils::get_string(root.get(), "gateway_hostname");
        bool enable_rds_aad_auth = json_utils::get_bool(root.get(), "enable_rds_aad_auth");
        bool target_is_aad_joined = json_utils::get_bool(root.get(), "target_is_aad_joined");
        std::string load_balance_info = json_utils::get_string(root.get(), "load_balance_info");
        
        // Additional AVD parameters
        std::string aad_tenant_id = json_utils::get_string(root.get(), "aad_tenant_id");
        std::string wvd_endpoint_pool = json_utils::get_string(root.get(), "wvd_endpoint_pool");
        std::string arm_path = json_utils::get_string(root.get(), "arm_path");
        std::string workspace_id = json_utils::get_string(root.get(), "workspace_id");
        std::string remote_application_program = json_utils::get_string(root.get(), "remote_application_program");
        
        LOG_INFO("RDPMAN", "Initiating RDP connection to " << host << ":" << port);
    
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
            LOG_INFO("RDPMAN", "AVD/Dev Box connection detected");
            LOG_DEBUG("RDPMAN", "  Gateway: " << params.gateway_hostname << ":" << params.gateway_port);
            LOG_DEBUG("RDPMAN", "  AAD Auth: " << (params.enable_rds_aad_auth ? "enabled" : "disabled"));
            if (!params.aad_tenant_id.empty()) {
                LOG_DEBUG("RDPMAN", "  AAD Tenant: " << params.aad_tenant_id);
            }
            if (!params.load_balance_info.empty()) {
                LOG_DEBUG("RDPMAN", "  Load Balance: " << params.load_balance_info);
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
        LOG_ERROR("RDPMAN", "Exception in connect_rdp: " << ex.what());
        e->return_string("{\"success\": false, \"error\": \"Internal error: " + json_utils::escape_string(ex.what()) + "\"}");
    } catch (...) {
        LOG_ERROR("RDPMAN", "Unknown exception in connect_rdp");
        e->return_string("{\"success\": false, \"error\": \"Unknown internal error\"}");
    }
}

void JSHandlers::s_get_connections(webui::window::event* e) {
    if (!s_instance) return;
    
    LOG_DEBUG("RDPMAN", "get_connections called");
    std::string json = s_instance->m_config_manager.get_connections_json();
    LOG_DEBUG("RDPMAN", "Returning " << json.size() << " bytes of JSON");
    e->return_string(json);
}

void JSHandlers::s_save_connection(webui::window::event* e) {
    if (!s_instance) return;
    
    try {
        // Get JSON string from JavaScript
        std::string json_str = e->get_string(0);
        
        LOG_DEBUG("RDPMAN", "save_connection called with JSON: " << json_str.substr(0, 100) << "...");
        
        if (json_str.empty()) {
            LOG_ERROR("RDPMAN", "save_connection: Empty JSON");
            e->return_bool(false);
            return;
        }
        
        // Parse JSON
        json_error_t error;
        json_utils::JsonPtr root{json_loads(json_str.c_str(), 0, &error)};
        if (!root) {
            LOG_ERROR("RDPMAN", "save_connection JSON parse error: " << error.text);
            e->return_bool(false);
            return;
        }
        
        // Build ConnectionProfile from JSON
        ConnectionProfile profile;
        profile.name = json_utils::get_string(root.get(), "name");
        profile.folder = json_utils::get_string(root.get(), "folder");
        profile.hostname = json_utils::get_string(root.get(), "hostname");
        profile.port = json_utils::get_int(root.get(), "port", 3389);
        profile.username = json_utils::get_string(root.get(), "username");
        profile.domain = json_utils::get_string(root.get(), "domain");
        
        LOG_DEBUG("RDPMAN", "save_connection: name='" << profile.name << "' host='" << profile.hostname << "' port=" << profile.port);
        
        // Advanced options
        profile.home_drive = json_utils::get_bool(root.get(), "home_drive");
        profile.clipboard = json_utils::get_bool(root.get(), "clipboard", true);
        profile.cert_tofu = json_utils::get_bool(root.get(), "cert_tofu");
        profile.usb_auto = json_utils::get_bool(root.get(), "usb_auto");
        profile.floatbar = json_utils::get_bool(root.get(), "floatbar");
        profile.dynamic_resolution = json_utils::get_bool(root.get(), "dynamic_resolution");
        profile.network_auto = json_utils::get_bool(root.get(), "network_auto");
        profile.gfx_avc420 = json_utils::get_bool(root.get(), "gfx_avc420");
        profile.compression = json_utils::get_bool(root.get(), "compression");
        profile.audio_pulse = json_utils::get_bool(root.get(), "audio_pulse");
        profile.prevent_session_lock = json_utils::get_bool(root.get(), "prevent_session_lock");
        profile.auto_reconnect = json_utils::get_bool(root.get(), "auto_reconnect");
        profile.auto_reconnect_max_retries = json_utils::get_int(root.get(), "auto_reconnect_max_retries", 3);
        if (profile.auto_reconnect_max_retries <= 0) {
            profile.auto_reconnect_max_retries = 3;
        }
        
        // AVD/Dev Box fields
        profile.remote_desktop_name = json_utils::get_string(root.get(), "remote_desktop_name");
        profile.wvd_endpoint_pool = json_utils::get_string(root.get(), "wvd_endpoint_pool");
        profile.workspace_id = json_utils::get_string(root.get(), "workspace_id");
        profile.arm_path = json_utils::get_string(root.get(), "arm_path");
        profile.remote_application_program = json_utils::get_string(root.get(), "remote_application_program");
        
        // Gateway / AAD fields
        profile.gateway_hostname = json_utils::get_string(root.get(), "gateway_hostname");
        profile.enable_rds_aad_auth = json_utils::get_bool(root.get(), "enable_rds_aad_auth");
        profile.target_is_aad_joined = json_utils::get_bool(root.get(), "target_is_aad_joined");
        profile.load_balance_info = json_utils::get_string(root.get(), "load_balance_info");
        profile.aad_tenant_id = json_utils::get_string(root.get(), "aad_tenant_id");
        
        // Inheritance tracking
        json_t* overrides_arr = json_object_get(root.get(), "overridden_fields");
        if (overrides_arr && json_is_array(overrides_arr)) {
            profile.legacy_profile = false;
            size_t idx;
            json_t* val;
            json_array_foreach(overrides_arr, idx, val) {
                if (json_is_string(val)) {
                    profile.overridden_fields.insert(json_string_value(val));
                }
            }
        } else {
            // No overridden_fields from frontend — treat as legacy (all overridden)
            profile.legacy_profile = true;
            for (const auto& name : inheritable_field_names()) {
                profile.overridden_fields.insert(name);
            }
        }
        
        bool success = s_instance->m_config_manager.save_connection(profile);
        
        LOG_INFO("RDPMAN", "save_connection result: " << (success ? "success" : "failed"));
        e->return_bool(success);
    } catch (const std::exception& ex) {
        LOG_ERROR("RDPMAN", "Exception in save_connection: " << ex.what());
        e->return_bool(false);
    } catch (...) {
        LOG_ERROR("RDPMAN", "Unknown exception in save_connection");
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
    
    LOG_DEBUG("RDPMAN", "Importing RDP file content (" << content.size() << " bytes)");
    
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

void JSHandlers::s_create_database(webui::window::event* e) {
    if (!s_instance) return;

    std::string path = e->get_string(0);
    bool success = s_instance->m_config_manager.create_database(path);
    e->return_bool(success);
}

void JSHandlers::s_create_database_dialog(webui::window::event* e) {
    if (!s_instance) return;

    const std::string path = pick_database_save_path();
    if (path.empty()) {
        e->return_bool(false);
        return;
    }

    bool success = s_instance->m_config_manager.create_database(path);
    e->return_bool(success);
}

void JSHandlers::s_open_database(webui::window::event* e) {
    if (!s_instance) return;

    std::string path = e->get_string(0);
    bool success = s_instance->m_config_manager.open_database(path);
    e->return_bool(success);
}

void JSHandlers::s_open_database_dialog(webui::window::event* e) {
    if (!s_instance) return;

    const std::string path = pick_database_file_path();
    if (path.empty()) {
        e->return_bool(false);
        return;
    }

    bool success = s_instance->m_config_manager.open_database(path);
    e->return_bool(success);
}

void JSHandlers::s_clone_database(webui::window::event* e) {
    if (!s_instance) return;

    std::string source_path = e->get_string(0);
    std::string target_path = e->get_string(1);
    bool success = s_instance->m_config_manager.clone_database(source_path, target_path);
    e->return_bool(success);
}

void JSHandlers::s_close_database(webui::window::event* e) {
    if (!s_instance) return;

    bool success = s_instance->m_config_manager.close_database();
    e->return_bool(success);
}

void JSHandlers::s_get_database_status(webui::window::event* e) {
    if (!s_instance) return;

    const bool is_open = s_instance->m_config_manager.has_open_database();
    const auto path = json_utils::escape_string(s_instance->m_config_manager.get_database_path());
    auto result = std::format(R"({{"isOpen":{},"path":"{}"}})",
                              is_open ? "true" : "false", path);
    e->return_string(result);
}

void JSHandlers::s_create_folder(webui::window::event* e) {
    if (!s_instance) return;

    std::string folder = e->get_string(0);
    bool success = s_instance->m_config_manager.create_folder(folder);
    e->return_bool(success);
}

void JSHandlers::s_move_folder(webui::window::event* e) {
    if (!s_instance) return;

    std::string source_folder = e->get_string(0);
    std::string target_parent_folder = e->get_string(1);
    bool success = s_instance->m_config_manager.move_folder(source_folder, target_parent_folder);
    e->return_bool(success);
}

void JSHandlers::s_rename_folder(webui::window::event* e) {
    if (!s_instance) return;

    std::string source_folder = e->get_string(0);
    std::string new_name = e->get_string(1);
    bool success = s_instance->m_config_manager.rename_folder(source_folder, new_name);
    e->return_bool(success);
}

void JSHandlers::s_delete_folder(webui::window::event* e) {
    if (!s_instance) return;

    std::string folder = e->get_string(0);
    bool success = s_instance->m_config_manager.delete_folder(folder);
    e->return_bool(success);
}

void JSHandlers::s_get_folders(webui::window::event* e) {
    if (!s_instance) return;

    e->return_string(s_instance->m_config_manager.get_folders_json());
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

// ============================================================================
// Feed Discovery Handlers
// ============================================================================

void JSHandlers::s_get_feed_accounts(webui::window::event* e) {
    if (!s_instance) return;

    e->return_string(s_instance->m_config_manager.get_feed_accounts_json());
}

void JSHandlers::s_delete_feed_account(webui::window::event* e) {
    if (!s_instance) return;

    std::string id = e->get_string(0);
    bool success = s_instance->m_config_manager.delete_feed_account(id);
    e->return_bool(success);
}

void JSHandlers::s_discover_feeds(webui::window::event* e) {
    if (!s_instance) return;

    // account_id is optional: empty string means "add new account"
    std::string account_id = e->get_string(0);

    if (s_instance->m_feed_discovery.is_busy()) {
        e->return_string("{\"success\": false, \"error\": \"Discovery already in progress\"}");
        return;
    }

    // Run discovery (blocks until complete - popup + HTTP calls)
    auto result = s_instance->m_feed_discovery.discover_and_import(account_id);

    auto json = std::format(
        R"({{"success":{},{}"imported_count":{},"tenant_count":{},"account_id":"{}","account_display_name":"{}"}})",
        result.success ? "true" : "false",
        result.error.empty() ? "" : std::format(R"("error":"{}",)", json_utils::escape_string(result.error)).c_str(),
        result.imported_count, result.tenant_count,
        json_utils::escape_string(result.account_id),
        json_utils::escape_string(result.account_display_name)
    );
    e->return_string(json);
}

void JSHandlers::s_log_off_account(webui::window::event* e) {
    if (!s_instance) return;

    std::string id = e->get_string(0);
    bool success = s_instance->m_config_manager.clear_tokens_for_account(id);
    e->return_bool(success);
}

void JSHandlers::s_forget_account(webui::window::event* e) {
    if (!s_instance) return;

    std::string id = e->get_string(0);
    bool success = s_instance->m_config_manager.forget_account(id);
    e->return_bool(success);
}

// ============================================================================
// Folder Settings Handlers (parameter inheritance)
// ============================================================================

void JSHandlers::s_get_folder_settings(webui::window::event* e) {
    if (!s_instance) return;

    std::string path = e->get_string(0);
    e->return_string(s_instance->m_config_manager.get_folder_settings_json(path));
}

void JSHandlers::s_save_folder_settings(webui::window::event* e) {
    if (!s_instance) return;

    try {
        std::string path = e->get_string(0);
        std::string json_str = e->get_string(1);

        if (json_str.empty()) {
            e->return_bool(false);
            return;
        }

        json_error_t error;
        json_utils::JsonPtr root{json_loads(json_str.c_str(), 0, &error)};
        if (!root) {
            LOG_ERROR("RDPMAN", "saveFolderSettings JSON parse error: " << error.text);
            e->return_bool(false);
            return;
        }

        // Build FolderSettings from parsed JSON using the same static helper used internally
        FolderSettings settings;
        auto get_opt_bool = [&](const char* key) -> std::optional<bool> {
            json_t* val = json_object_get(root.get(), key);
            if (val && json_is_boolean(val)) return json_is_true(val);
            return std::nullopt;
        };
        auto get_opt_int = [&](const char* key) -> std::optional<int> {
            json_t* val = json_object_get(root.get(), key);
            if (val && json_is_integer(val)) return static_cast<int>(json_integer_value(val));
            return std::nullopt;
        };
        auto get_opt_str = [&](const char* key) -> std::optional<std::string> {
            json_t* val = json_object_get(root.get(), key);
            if (val && json_is_string(val)) return std::string(json_string_value(val));
            return std::nullopt;
        };

        settings.home_drive = get_opt_bool("home_drive");
        settings.clipboard = get_opt_bool("clipboard");
        settings.cert_tofu = get_opt_bool("cert_tofu");
        settings.usb_auto = get_opt_bool("usb_auto");
        settings.floatbar = get_opt_bool("floatbar");
        settings.dynamic_resolution = get_opt_bool("dynamic_resolution");
        settings.network_auto = get_opt_bool("network_auto");
        settings.gfx_avc420 = get_opt_bool("gfx_avc420");
        settings.compression = get_opt_bool("compression");
        settings.audio_pulse = get_opt_bool("audio_pulse");
        settings.prevent_session_lock = get_opt_bool("prevent_session_lock");
        settings.auto_reconnect = get_opt_bool("auto_reconnect");
        settings.auto_reconnect_max_retries = get_opt_int("auto_reconnect_max_retries");
        settings.gateway_hostname = get_opt_str("gateway_hostname");
        settings.enable_rds_aad_auth = get_opt_bool("enable_rds_aad_auth");
        settings.target_is_aad_joined = get_opt_bool("target_is_aad_joined");
        settings.load_balance_info = get_opt_str("load_balance_info");

        bool success = s_instance->m_config_manager.save_folder_settings(path, settings);
        e->return_bool(success);
    } catch (const std::exception& ex) {
        LOG_ERROR("RDPMAN", "Exception in saveFolderSettings: " << ex.what());
        e->return_bool(false);
    }
}

void JSHandlers::s_get_effective_folder_settings(webui::window::event* e) {
    if (!s_instance) return;

    std::string path = e->get_string(0);
    e->return_string(s_instance->m_config_manager.get_effective_folder_settings_json(path));
}

void JSHandlers::s_get_effective_connection_profile(webui::window::event* e) {
    if (!s_instance) return;

    std::string name = e->get_string(0);
    e->return_string(s_instance->m_config_manager.get_effective_connection_profile_json(name));
}
