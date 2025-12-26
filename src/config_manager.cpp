/**
 * Configuration Manager Implementation
 * 
 * Uses a simple JSON format for storing connection profiles.
 */

#include "config_manager.hpp"

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
    
    // Simple JSON parsing (no external dependency)
    // Format: [{"name":"...", "hostname":"...", ...}, ...]
    m_connections.clear();
    
    // Find array content
    size_t array_start = content.find('[');
    size_t array_end = content.rfind(']');
    
    if (array_start == std::string::npos || array_end == std::string::npos) {
        return true;  // Empty or invalid, start fresh
    }
    
    // Parse each object
    size_t pos = array_start;
    while (pos < array_end) {
        size_t obj_start = content.find('{', pos);
        if (obj_start == std::string::npos || obj_start >= array_end) break;
        
        size_t obj_end = content.find('}', obj_start);
        if (obj_end == std::string::npos) break;
        
        std::string obj = content.substr(obj_start, obj_end - obj_start + 1);
        
        ConnectionProfile profile;
        
        // Extract fields (simple string extraction)
        // Helper to skip whitespace
        auto skip_whitespace = [&obj](size_t pos) -> size_t {
            while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t' || obj[pos] == '\n' || obj[pos] == '\r')) {
                pos++;
            }
            return pos;
        };
        
        auto extract_string = [&obj, &skip_whitespace](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":";
            size_t start = obj.find(search);
            if (start == std::string::npos) return "";
            start += search.length();
            start = skip_whitespace(start);
            if (start >= obj.size() || obj[start] != '"') return "";
            start++; // skip opening quote
            size_t end = obj.find('"', start);
            if (end == std::string::npos) return "";
            return obj.substr(start, end - start);
        };
        
        auto extract_int = [&obj, &skip_whitespace](const std::string& key) -> int {
            std::string search = "\"" + key + "\":";
            size_t start = obj.find(search);
            if (start == std::string::npos) return 0;
            start += search.length();
            start = skip_whitespace(start);
            try {
                return std::stoi(obj.substr(start));
            } catch (...) {
                return 0;
            }
        };
        
        auto extract_bool = [&obj, &skip_whitespace](const std::string& key) -> bool {
            std::string search = "\"" + key + "\":";
            size_t start = obj.find(search);
            if (start == std::string::npos) return false;
            start += search.length();
            start = skip_whitespace(start);
            return obj.substr(start, 4) == "true";
        };
        
        profile.name = extract_string("name");
        profile.hostname = extract_string("hostname");
        profile.port = extract_int("port");
        profile.username = extract_string("username");
        profile.domain = extract_string("domain");
        profile.width = extract_int("width");
        profile.height = extract_int("height");
        profile.fullscreen = extract_bool("fullscreen");
        
        // Advanced RDP Options
        profile.home_drive = extract_bool("home_drive");
        profile.clipboard = extract_bool("clipboard");
        profile.cert_tofu = extract_bool("cert_tofu");
        profile.usb_auto = extract_bool("usb_auto");
        profile.floatbar = extract_bool("floatbar");
        profile.dynamic_resolution = extract_bool("dynamic_resolution");
        profile.network_auto = extract_bool("network_auto");
        profile.gfx_avc420 = extract_bool("gfx_avc420");
        profile.compression = extract_bool("compression");
        profile.audio_pulse = extract_bool("audio_pulse");
        profile.prevent_session_lock = extract_bool("prevent_session_lock");
        profile.auto_reconnect = extract_bool("auto_reconnect");
        profile.auto_reconnect_max_retries = extract_int("auto_reconnect_max_retries");
        
        if (profile.port == 0) profile.port = 3389;
        if (profile.width == 0) profile.width = 1920;
        if (profile.height == 0) profile.height = 1080;
        if (profile.auto_reconnect_max_retries == 0) profile.auto_reconnect_max_retries = 3;
        // Default clipboard to true for older saved connections
        if (obj.find("\"clipboard\"") == std::string::npos) profile.clipboard = true;
        
        if (!profile.name.empty() && !profile.hostname.empty()) {
            m_connections.push_back(profile);
        }
        
        pos = obj_end + 1;
    }
    
    std::cout << "[ConfigManager] Loaded " << m_connections.size() << " connections" << std::endl;
    return true;
}

bool ConfigManager::save() {
    // Build JSON string
    std::stringstream json;
    json << "[\n";
    
    for (size_t i = 0; i < m_connections.size(); ++i) {
        const auto& c = m_connections[i];
        
        json << "  {\n";
        json << "    \"name\": \"" << c.name << "\",\n";
        json << "    \"hostname\": \"" << c.hostname << "\",\n";
        json << "    \"port\": " << c.port << ",\n";
        json << "    \"username\": \"" << c.username << "\",\n";
        json << "    \"domain\": \"" << c.domain << "\",\n";
        json << "    \"width\": " << c.width << ",\n";
        json << "    \"height\": " << c.height << ",\n";
        json << "    \"fullscreen\": " << (c.fullscreen ? "true" : "false") << ",\n";
        // Advanced RDP Options
        json << "    \"home_drive\": " << (c.home_drive ? "true" : "false") << ",\n";
        json << "    \"clipboard\": " << (c.clipboard ? "true" : "false") << ",\n";
        json << "    \"cert_tofu\": " << (c.cert_tofu ? "true" : "false") << ",\n";
        json << "    \"usb_auto\": " << (c.usb_auto ? "true" : "false") << ",\n";
        json << "    \"floatbar\": " << (c.floatbar ? "true" : "false") << ",\n";
        json << "    \"dynamic_resolution\": " << (c.dynamic_resolution ? "true" : "false") << ",\n";
        json << "    \"network_auto\": " << (c.network_auto ? "true" : "false") << ",\n";
        json << "    \"gfx_avc420\": " << (c.gfx_avc420 ? "true" : "false") << ",\n";
        json << "    \"compression\": " << (c.compression ? "true" : "false") << ",\n";
        json << "    \"audio_pulse\": " << (c.audio_pulse ? "true" : "false") << ",\n";
        json << "    \"prevent_session_lock\": " << (c.prevent_session_lock ? "true" : "false") << ",\n";
        json << "    \"auto_reconnect\": " << (c.auto_reconnect ? "true" : "false") << ",\n";
        json << "    \"auto_reconnect_max_retries\": " << c.auto_reconnect_max_retries << "\n";
        json << "  }";
        
        if (i < m_connections.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    
    json << "]\n";
    
    // Write to file
    std::ofstream file(m_config_path);
    if (!file.is_open()) {
        std::cerr << "[ConfigManager] Failed to write config file" << std::endl;
        return false;
    }
    
    file << json.str();
    file.close();
    
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

ConnectionProfile* ConfigManager::get_connection(const std::string& name) {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&name](const ConnectionProfile& p) { return p.name == name; });
    
    if (it != m_connections.end()) {
        return &(*it);
    }
    return nullptr;
}

std::string ConfigManager::get_connections_json() const {
    std::stringstream json;
    json << "[";
    
    for (size_t i = 0; i < m_connections.size(); ++i) {
        const auto& c = m_connections[i];
        
        json << "{";
        json << "\"name\":\"" << c.name << "\",";
        json << "\"hostname\":\"" << c.hostname << "\",";
        json << "\"port\":" << c.port << ",";
        json << "\"username\":\"" << c.username << "\",";
        json << "\"domain\":\"" << c.domain << "\",";
        // Advanced RDP Options
        json << "\"home_drive\":" << (c.home_drive ? "true" : "false") << ",";
        json << "\"clipboard\":" << (c.clipboard ? "true" : "false") << ",";
        json << "\"cert_tofu\":" << (c.cert_tofu ? "true" : "false") << ",";
        json << "\"usb_auto\":" << (c.usb_auto ? "true" : "false") << ",";
        json << "\"floatbar\":" << (c.floatbar ? "true" : "false") << ",";
        json << "\"dynamic_resolution\":" << (c.dynamic_resolution ? "true" : "false") << ",";
        json << "\"network_auto\":" << (c.network_auto ? "true" : "false") << ",";
        json << "\"gfx_avc420\":" << (c.gfx_avc420 ? "true" : "false") << ",";
        json << "\"compression\":" << (c.compression ? "true" : "false") << ",";
        json << "\"audio_pulse\":" << (c.audio_pulse ? "true" : "false") << ",";
        json << "\"prevent_session_lock\":" << (c.prevent_session_lock ? "true" : "false") << ",";
        json << "\"auto_reconnect\":" << (c.auto_reconnect ? "true" : "false") << ",";
        json << "\"auto_reconnect_max_retries\":" << c.auto_reconnect_max_retries;
        json << "}";
        
        if (i < m_connections.size() - 1) {
            json << ",";
        }
    }
    
    json << "]";
    auto serializedJson = json.str();
    return serializedJson;
}
