#pragma once

/**
 * Configuration Manager
 * 
 * Handles saving and loading RDP connection profiles.
 */

#include <string>
#include <vector>
#include <optional>
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
    
    // Gateway / AAD settings
    std::string gateway_hostname;      // RD Gateway hostname
    bool enable_rds_aad_auth = false;  // Enable RDS AAD authentication
    bool target_is_aad_joined = false; // Target is Azure AD joined
    std::string load_balance_info;     // Load balance info string
    
    // AVD/Dev Box specific fields
    std::string remote_desktop_name;   // Display name for the connection
    std::string wvd_endpoint_pool;     // WVD endpoint pool ID
    std::string workspace_id;          // Azure workspace ID
    std::string arm_path;              // Azure Resource Manager path
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
    bool save_connection(const ConnectionProfile& profile);
    
    /**
     * Delete a connection by name
     */
    bool delete_connection(const std::string& name);
    
    /**
     * Get connection by name
     */
    std::optional<ConnectionProfile> get_connection(const std::string& name) const;
    
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
