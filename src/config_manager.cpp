/**
 * Configuration Manager Implementation
 * 
 * Uses jansson for JSON serialization/deserialization of connection profiles.
 */

#include "config_manager.hpp"
#include "json_utils.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#endif

ConfigManager::ConfigManager() {
    m_config_path = get_config_file_path();
    
    // Ensure config directory exists
    fs::path config_dir = get_config_directory();
    if (!fs::exists(config_dir)) {
        fs::create_directories(config_dir);
    }
    
    // Load existing connections
    load();
}

fs::path ConfigManager::get_config_directory() const {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        return fs::path(path) / "webui-rdp-client";
    }
    return fs::path(".") / ".webui-rdp-client";
#else
    const char* home = std::getenv("HOME");
    if (home) {
        // Check for XDG config directory
        const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
        if (xdg_config) {
            return fs::path(xdg_config) / "webui-rdp-client";
        }
        return fs::path(home) / ".config" / "webui-rdp-client";
    }
    return fs::path(".") / ".webui-rdp-client";
#endif
}

fs::path ConfigManager::get_config_file_path() const {
    fs::path configFilePath = get_config_directory() / "connections.json";
    return configFilePath;
}

// ============================================================================
// Helper: populate a ConnectionProfile from a jansson object
// ============================================================================
static ConnectionProfile profile_from_json(json_t* obj) {
    ConnectionProfile p;
    p.name       = json_utils::get_string(obj, "name");
    p.hostname   = json_utils::get_string(obj, "hostname");
    p.port       = json_utils::get_int(obj, "port", 3389);
    p.username   = json_utils::get_string(obj, "username");
    p.domain     = json_utils::get_string(obj, "domain");
    p.width      = json_utils::get_int(obj, "width", 1920);
    p.height     = json_utils::get_int(obj, "height", 1080);
    p.fullscreen = json_utils::get_bool(obj, "fullscreen");

    // Advanced RDP Options
    // Default clipboard to true when key is absent (older saved profiles)
    p.home_drive           = json_utils::get_bool(obj, "home_drive");
    p.clipboard            = json_utils::get_bool(obj, "clipboard",
                                json_object_get(obj, "clipboard") ? false : true);
    p.cert_tofu            = json_utils::get_bool(obj, "cert_tofu");
    p.usb_auto             = json_utils::get_bool(obj, "usb_auto");
    p.floatbar             = json_utils::get_bool(obj, "floatbar");
    p.dynamic_resolution   = json_utils::get_bool(obj, "dynamic_resolution");
    p.network_auto         = json_utils::get_bool(obj, "network_auto");
    p.gfx_avc420           = json_utils::get_bool(obj, "gfx_avc420");
    p.compression          = json_utils::get_bool(obj, "compression");
    p.audio_pulse          = json_utils::get_bool(obj, "audio_pulse");
    p.prevent_session_lock = json_utils::get_bool(obj, "prevent_session_lock");
    p.auto_reconnect       = json_utils::get_bool(obj, "auto_reconnect");
    p.auto_reconnect_max_retries = json_utils::get_int(obj, "auto_reconnect_max_retries", 3);

    // Gateway / AAD fields
    p.gateway_hostname     = json_utils::get_string(obj, "gateway_hostname");
    p.enable_rds_aad_auth  = json_utils::get_bool(obj, "enable_rds_aad_auth");
    p.target_is_aad_joined = json_utils::get_bool(obj, "target_is_aad_joined");
    p.load_balance_info    = json_utils::get_string(obj, "load_balance_info");

    // AVD/Dev Box fields
    p.remote_desktop_name = json_utils::get_string(obj, "remote_desktop_name");
    p.wvd_endpoint_pool   = json_utils::get_string(obj, "wvd_endpoint_pool");
    p.workspace_id        = json_utils::get_string(obj, "workspace_id");
    p.arm_path            = json_utils::get_string(obj, "arm_path");
    p.remote_application_program = json_utils::get_string(obj, "remote_application_program");
    p.aad_tenant_id   = json_utils::get_string(obj, "aad_tenant_id");

    // Clamp defaults
    if (p.port   <= 0) p.port   = 3389;
    if (p.width  <= 0) p.width  = 1920;
    if (p.height <= 0) p.height = 1080;
    if (p.auto_reconnect_max_retries <= 0) p.auto_reconnect_max_retries = 3;

    return p;
}

// ============================================================================
// Helper: serialize a ConnectionProfile to a jansson object
// ============================================================================
static json_t* profile_to_json(const ConnectionProfile& c) {
    json_t* obj = json_object();
    json_object_set_new(obj, "name",       json_string(c.name.c_str()));
    json_object_set_new(obj, "hostname",   json_string(c.hostname.c_str()));
    json_object_set_new(obj, "port",       json_integer(c.port));
    json_object_set_new(obj, "username",   json_string(c.username.c_str()));
    json_object_set_new(obj, "domain",     json_string(c.domain.c_str()));
    json_object_set_new(obj, "width",      json_integer(c.width));
    json_object_set_new(obj, "height",     json_integer(c.height));
    json_object_set_new(obj, "fullscreen", json_boolean(c.fullscreen));
    // Advanced RDP Options
    json_object_set_new(obj, "home_drive",           json_boolean(c.home_drive));
    json_object_set_new(obj, "clipboard",            json_boolean(c.clipboard));
    json_object_set_new(obj, "cert_tofu",            json_boolean(c.cert_tofu));
    json_object_set_new(obj, "usb_auto",             json_boolean(c.usb_auto));
    json_object_set_new(obj, "floatbar",             json_boolean(c.floatbar));
    json_object_set_new(obj, "dynamic_resolution",   json_boolean(c.dynamic_resolution));
    json_object_set_new(obj, "network_auto",         json_boolean(c.network_auto));
    json_object_set_new(obj, "gfx_avc420",           json_boolean(c.gfx_avc420));
    json_object_set_new(obj, "compression",          json_boolean(c.compression));
    json_object_set_new(obj, "audio_pulse",          json_boolean(c.audio_pulse));
    json_object_set_new(obj, "prevent_session_lock", json_boolean(c.prevent_session_lock));
    json_object_set_new(obj, "auto_reconnect",       json_boolean(c.auto_reconnect));
    json_object_set_new(obj, "auto_reconnect_max_retries", json_integer(c.auto_reconnect_max_retries));
    // Gateway / AAD fields
    json_object_set_new(obj, "gateway_hostname",     json_string(c.gateway_hostname.c_str()));
    json_object_set_new(obj, "enable_rds_aad_auth",  json_boolean(c.enable_rds_aad_auth));
    json_object_set_new(obj, "target_is_aad_joined", json_boolean(c.target_is_aad_joined));
    json_object_set_new(obj, "load_balance_info",    json_string(c.load_balance_info.c_str()));
    // AVD/Dev Box fields
    json_object_set_new(obj, "remote_desktop_name", json_string(c.remote_desktop_name.c_str()));
    json_object_set_new(obj, "wvd_endpoint_pool",   json_string(c.wvd_endpoint_pool.c_str()));
    json_object_set_new(obj, "workspace_id",        json_string(c.workspace_id.c_str()));
    json_object_set_new(obj, "arm_path",            json_string(c.arm_path.c_str()));
    json_object_set_new(obj, "remote_application_program", json_string(c.remote_application_program.c_str()));
    json_object_set_new(obj, "aad_tenant_id",     json_string(c.aad_tenant_id.c_str()));
    return obj;
}

// ============================================================================
// Load / Save
// ============================================================================

bool ConfigManager::load() {
    if (!fs::exists(m_config_path)) {
        std::cout << "[ConfigManager] No config file found at " << m_config_path << std::endl;
        return true;  // Not an error, just no saved connections
    }
    
    std::ifstream file(m_config_path);
    if (!file.is_open()) {
        std::cerr << "[ConfigManager] Failed to open config file" << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    m_connections.clear();
    
    json_error_t error;
    json_t* root = json_loads(content.c_str(), 0, &error);
    if (!root) {
        std::cerr << "[ConfigManager] JSON parse error at line " << error.line
                  << ": " << error.text << std::endl;
        return false;
    }
    
    if (!json_is_array(root)) {
        std::cerr << "[ConfigManager] Config file root is not an array" << std::endl;
        json_decref(root);
        return false;
    }
    
    size_t index;
    json_t* value;
    json_array_foreach(root, index, value) {
        if (!json_is_object(value)) continue;
        
        ConnectionProfile profile = profile_from_json(value);
        if (!profile.name.empty() && !profile.hostname.empty()) {
            m_connections.push_back(std::move(profile));
        }
    }
    
    json_decref(root);
    std::cout << "[ConfigManager] Loaded " << m_connections.size() << " connections" << std::endl;
    return true;
}

bool ConfigManager::save() {
    // Build JSON array
    json_t* root = json_array();
    for (const auto& c : m_connections) {
        json_array_append_new(root, profile_to_json(c));
    }
    
    char* dump = json_dumps(root, JSON_INDENT(2) | JSON_SORT_KEYS);
    json_decref(root);
    
    if (!dump) {
        std::cerr << "[ConfigManager] Failed to serialize config to JSON" << std::endl;
        return false;
    }
    
    std::string json_str(dump);
    free(dump);
    
    // Atomic write: write to temp file, then rename over the target
    fs::path tmp_path = m_config_path;
    tmp_path += ".tmp";
    
    {
        std::ofstream file(tmp_path);
        if (!file.is_open()) {
            std::cerr << "[ConfigManager] Failed to write temp config file" << std::endl;
            return false;
        }
        file << json_str << "\n";
    }
    
    std::error_code ec;
    fs::rename(tmp_path, m_config_path, ec);
    if (ec) {
        std::cerr << "[ConfigManager] Failed to rename temp config: " << ec.message() << std::endl;
        fs::remove(tmp_path, ec);
        return false;
    }
    
    std::cout << "[ConfigManager] Saved " << m_connections.size() << " connections to " << m_config_path << std::endl;
    return true;
}

bool ConfigManager::save_connection(const ConnectionProfile& profile) {
    // Check if connection with this name exists
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&profile](const ConnectionProfile& p) { return p.name == profile.name; });
    
    if (it != m_connections.end()) {
        // Update existing - preserve name, update everything else
        it->hostname = profile.hostname;
        it->port = (profile.port > 0) ? profile.port : 3389;
        it->username = profile.username;
        it->domain = profile.domain;
        it->width = profile.width;
        it->height = profile.height;
        it->fullscreen = profile.fullscreen;
        it->home_drive = profile.home_drive;
        it->clipboard = profile.clipboard;
        it->cert_tofu = profile.cert_tofu;
        it->usb_auto = profile.usb_auto;
        it->floatbar = profile.floatbar;
        it->dynamic_resolution = profile.dynamic_resolution;
        it->network_auto = profile.network_auto;
        it->gfx_avc420 = profile.gfx_avc420;
        it->compression = profile.compression;
        it->audio_pulse = profile.audio_pulse;
        it->prevent_session_lock = profile.prevent_session_lock;
        it->auto_reconnect = profile.auto_reconnect;
        it->auto_reconnect_max_retries = profile.auto_reconnect_max_retries;
        it->gateway_hostname = profile.gateway_hostname;
        it->enable_rds_aad_auth = profile.enable_rds_aad_auth;
        it->target_is_aad_joined = profile.target_is_aad_joined;
        it->load_balance_info = profile.load_balance_info;
        it->remote_desktop_name = profile.remote_desktop_name;
        it->wvd_endpoint_pool = profile.wvd_endpoint_pool;
        it->workspace_id = profile.workspace_id;
        it->arm_path = profile.arm_path;
        it->remote_application_program = profile.remote_application_program;
        it->aad_tenant_id = profile.aad_tenant_id;
    } else {
        // Add new - copy the profile and fix port if needed
        ConnectionProfile new_profile = profile;
        if (new_profile.port <= 0) {
            new_profile.port = 3389;
        }
        m_connections.push_back(new_profile);
    }
    
    auto saveResult = save();
    return saveResult;
}

bool ConfigManager::delete_connection(const std::string& name) {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&name](const ConnectionProfile& p) { return p.name == name; });
    
    if (it == m_connections.end()) {
        return false;  // Not found
    }
    
    m_connections.erase(it);
    auto saveResult = save();
    return saveResult;
}

std::optional<ConnectionProfile> ConfigManager::get_connection(const std::string& name) const {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&name](const ConnectionProfile& p) { return p.name == name; });
    
    if (it != m_connections.end()) {
        return *it;
    }
    return std::nullopt;
}

std::string ConfigManager::get_connections_json() const {
    json_t* root = json_array();
    for (const auto& c : m_connections) {
        json_array_append_new(root, profile_to_json(c));
    }
    
    char* dump = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    
    std::string result(dump ? dump : "[]");
    free(dump);
    return result;
}
