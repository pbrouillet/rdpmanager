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
#include <cstdint>

struct sqlite3;

namespace fs = std::filesystem;

/**
 * Saved connection profile
 */
struct ConnectionProfile {
    std::string name;
    std::string folder;
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
    std::string remote_application_program; // Remote application program (e.g. ||<GUID>)
    std::string aad_tenant_id;             // AAD tenant ID for Azure authentication
};

/**
 * Saved feed account for WVD feed discovery
 */
struct FeedAccount {
    std::string id;                    // UUID identifier
    std::string display_name;          // Display name / label
    std::string email;                 // Account email (informational)
    std::string refresh_token;         // Cached refresh token (for future use)
    int64_t last_synced = 0;           // Epoch seconds of last sync
};

/**
 * Configuration Manager class
 */
class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    
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
     * Create a new database file (or open if it already exists)
     */
    bool create_database(const std::string& path);

    /**
     * Open an existing database file
     */
    bool open_database(const std::string& path);

    /**
     * Clone a database file to a new path and open the clone
     */
    bool clone_database(const std::string& source_path, const std::string& target_path);

    /**
     * Close the current database
     */
    bool close_database();

    /**
     * Get whether a database is currently open
     */
    bool has_open_database() const;

    /**
     * Get current database path (empty string if none)
     */
    std::string get_database_path() const;

    /**
     * Create/register a folder path
     */
    bool create_folder(const std::string& folder);

    /**
     * Move folder under a new parent folder (empty parent means root)
     */
    bool move_folder(const std::string& source_folder, const std::string& target_parent_folder);

    /**
     * Rename a folder (renames all nested paths too)
     */
    bool rename_folder(const std::string& source_folder, const std::string& new_name);

    /**
     * Delete a folder, all subfolders, and all contained connections
     */
    bool delete_folder(const std::string& folder);

    /**
     * Get folders as JSON array
     */
    std::string get_folders_json() const;

    /**
     * Lookup cached AAD token by hostname and cache kind (e.g. gateway/machine)
     */
    std::optional<std::string> get_cached_token(const std::string& hostname,
                                                const std::string& cache_kind) const;

    /**
     * Store cached AAD token with expiration epoch seconds
     */
    bool set_cached_token(const std::string& hostname,
                          const std::string& cache_kind,
                          const std::string& token,
                          int64_t expires_at_epoch);

    /**
     * Remove cached token entry
     */
    bool delete_cached_token(const std::string& hostname,
                             const std::string& cache_kind);
    
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

    // ========================================================================
    // Feed Account Management
    // ========================================================================

    /**
     * Add a new feed account
     */
    bool add_feed_account(const FeedAccount& account);

    /**
     * Update an existing feed account
     */
    bool update_feed_account(const FeedAccount& account);

    /**
     * Delete a feed account by id
     */
    bool delete_feed_account(const std::string& id);

    /**
     * Get all feed accounts
     */
    std::vector<FeedAccount> get_feed_accounts() const;

    /**
     * Get all feed accounts as JSON string
     */
    std::string get_feed_accounts_json() const;
    
private:
    std::vector<ConnectionProfile> m_connections;
    std::vector<std::string> m_folders;
    fs::path m_config_path;
    fs::path m_settings_path;
    fs::path m_database_path;
    sqlite3* m_db = nullptr;
    std::string m_last_database_path;
    
    fs::path get_config_directory() const;
    fs::path get_config_file_path() const;

    bool open_database_internal(const fs::path& path);
    bool ensure_schema();
    bool load_folders();
    bool save_folders();
    bool load_legacy_json();
    bool load_settings();
    bool save_settings() const;
    void set_last_database_path(const std::string& path);
    std::string get_last_database_path() const;
};
