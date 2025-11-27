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
    return get_config_directory() / "connections.json";
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
        auto extract_string = [&obj](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            size_t start = obj.find(search);
            if (start == std::string::npos) return "";
            start += search.length();
            size_t end = obj.find('"', start);
            if (end == std::string::npos) return "";
            return obj.substr(start, end - start);
        };
        
        auto extract_int = [&obj](const std::string& key) -> int {
            std::string search = "\"" + key + "\":";
            size_t start = obj.find(search);
            if (start == std::string::npos) return 0;
            start += search.length();
            return std::stoi(obj.substr(start));
        };
        
        auto extract_bool = [&obj](const std::string& key) -> bool {
            std::string search = "\"" + key + "\":";
            size_t start = obj.find(search);
            if (start == std::string::npos) return false;
            start += search.length();
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
        
        if (profile.port == 0) profile.port = 3389;
        if (profile.width == 0) profile.width = 1920;
        if (profile.height == 0) profile.height = 1080;
        
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
        json << "    \"fullscreen\": " << (c.fullscreen ? "true" : "false") << "\n";
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

bool ConfigManager::save_connection(const std::string& name,
                                   const std::string& host,
                                   int port,
                                   const std::string& username,
                                   const std::string& domain) {
    // Check if connection with this name exists
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&name](const ConnectionProfile& p) { return p.name == name; });
    
    if (it != m_connections.end()) {
        // Update existing
        it->hostname = host;
        it->port = (port > 0) ? port : 3389;
        it->username = username;
        it->domain = domain;
    } else {
        // Add new
        ConnectionProfile profile;
        profile.name = name;
        profile.hostname = host;
        profile.port = (port > 0) ? port : 3389;
        profile.username = username;
        profile.domain = domain;
        m_connections.push_back(profile);
    }
    
    return save();
}

bool ConfigManager::delete_connection(const std::string& name) {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&name](const ConnectionProfile& p) { return p.name == name; });
    
    if (it == m_connections.end()) {
        return false;  // Not found
    }
    
    m_connections.erase(it);
    return save();
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
        json << "\"domain\":\"" << c.domain << "\"";
        json << "}";
        
        if (i < m_connections.size() - 1) {
            json << ",";
        }
    }
    
    json << "]";
    return json.str();
}
