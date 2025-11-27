#pragma once

/**
 * Configuration Manager
 * 
 * Handles saving and loading RDP connection profiles.
 */

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

/**
 * Saved connection profile
 */
struct ConnectionProfile {
    std::string name;
    std::string hostname;
    int port = 3389;
    std::string username;
    std::string domain;
    
    // Display preferences
    int width = 1920;
    int height = 1080;
    bool fullscreen = false;
    
    // Advanced RDP Options
    bool home_drive = false;           // +home-drive
    bool clipboard = true;             // +clipboard
    bool cert_tofu = false;            // /cert:tofu
    bool usb_auto = false;             // /usb:auto
    bool floatbar = false;             // /floatbar
    bool dynamic_resolution = false;   // /dynamic-resolution
    bool network_auto = false;         // /network:auto
    bool gfx_avc420 = false;           // /gfx:AVC420
    bool compression = false;          // /compression
    bool audio_pulse = false;          // /audio:sys:pulse
    bool prevent_session_lock = false; // /prevent-session-lock
    bool auto_reconnect = false;       // /auto-reconnect
    int auto_reconnect_max_retries = 3; // /auto-reconnect-max-retries
};

/**
 * Configuration Manager class
 */
class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager() = default;
    
    /**
     * Load connections from config file
     */
    bool load();
    
    /**
     * Save connections to config file
     */
    bool save();
    
    /**
     * Save a single connection profile
     */
    bool save_connection(const std::string& name,
                        const std::string& host,
                        int port,
                        const std::string& username,
                        const std::string& domain,
                        bool home_drive = false,
                        bool clipboard = true,
                        bool cert_tofu = false,
                        bool usb_auto = false,
                        bool floatbar = false,
                        bool dynamic_resolution = false,
                        bool network_auto = false,
                        bool gfx_avc420 = false,
                        bool compression = false,
                        bool audio_pulse = false,
                        bool prevent_session_lock = false,
                        bool auto_reconnect = false,
                        int auto_reconnect_max_retries = 3);
    
    /**
     * Delete a connection by name
     */
    bool delete_connection(const std::string& name);
    
    /**
     * Get connection by name
     */
    ConnectionProfile* get_connection(const std::string& name);
    
    /**
     * Get all connections as JSON string (for frontend)
     */
    std::string get_connections_json() const;
    
    /**
     * Get list of all connection profiles
     */
    const std::vector<ConnectionProfile>& get_connections() const { return m_connections; }
    
private:
    std::vector<ConnectionProfile> m_connections;
    fs::path m_config_path;
    
    fs::path get_config_directory() const;
    fs::path get_config_file_path() const;
};
