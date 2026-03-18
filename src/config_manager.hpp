#pragma once

/**
 * Configuration Manager
 * 
 * Handles saving and loading RDP connection profiles.
 */

#include "connection_types.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <set>
#include <unordered_map>
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
    bool use_manual_code_flow = false;  // Use UI manual code flow for AAD auth
    
    // AVD/Dev Box specific fields
    std::string remote_desktop_name;   // Display name for the connection
    std::string wvd_endpoint_pool;     // WVD endpoint pool ID
    std::string workspace_id;          // Azure workspace ID
    std::string arm_path;              // Azure Resource Manager path
    std::string remote_application_program; // Remote application program (e.g. ||<GUID>)
    std::string aad_tenant_id;             // AAD tenant ID for Azure authentication
    std::string source_account_id;         // Feed account ID that imported this connection

    // Inheritance tracking: which inheritable fields are explicitly set on this connection.
    // If empty AND profile was loaded from an old DB (no overridden_fields key in JSON),
    // all inheritable fields are considered overridden for backward compatibility.
    std::set<std::string> overridden_fields;
    bool legacy_profile = false;  // true when loaded from old DB without overridden_fields
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
    [[nodiscard]] bool load();
    
    /**
     * Save connections to config file
     */
    [[nodiscard]] bool save();
    
    /**
     * Save a single connection profile
     */
    [[nodiscard]] bool save_connection(const ConnectionProfile& profile);
    
    /**
     * Delete a connection by name
     */
    [[nodiscard]] bool delete_connection(std::string_view name);

    /**
     * Create a new database file (or open if it already exists)
     */
    [[nodiscard]] bool create_database(std::string_view path);

    /**
     * Open an existing database file
     */
    [[nodiscard]] bool open_database(std::string_view path);

    /**
     * Clone a database file to a new path and open the clone
     */
    [[nodiscard]] bool clone_database(std::string_view source_path, std::string_view target_path);

    /**
     * Close the current database
     */
    [[nodiscard]] bool close_database();

    /**
     * Get whether a database is currently open
     */
    [[nodiscard]] bool has_open_database() const;

    /**
     * Get current database path (empty string if none)
     */
    [[nodiscard]] std::string get_database_path() const;

    /**
     * Create/register a folder path
     */
    [[nodiscard]] bool create_folder(std::string_view folder);

    /**
     * Move folder under a new parent folder (empty parent means root)
     */
    [[nodiscard]] bool move_folder(std::string_view source_folder, std::string_view target_parent_folder);

    /**
     * Rename a folder (renames all nested paths too)
     */
    [[nodiscard]] bool rename_folder(std::string_view source_folder, std::string_view new_name);

    /**
     * Delete a folder, all subfolders, and all contained connections
     */
    [[nodiscard]] bool delete_folder(std::string_view folder);

    /**
     * Get folders as JSON array
     */
    [[nodiscard]] std::string get_folders_json() const;

    // ========================================================================
    // Folder Settings (parameter inheritance)
    // ========================================================================

    /**
     * Save folder-level default settings (sparse: only explicitly set fields)
     */
    [[nodiscard]] bool save_folder_settings(const std::string& folder_path, const FolderSettings& settings);

    /**
     * Get a folder's own settings (sparse, not resolved)
     */
    [[nodiscard]] FolderSettings get_folder_settings(const std::string& folder_path) const;

    /**
     * Get a folder's own settings as JSON string
     */
    [[nodiscard]] std::string get_folder_settings_json(const std::string& folder_path) const;

    /**
     * Get effective (cascade-resolved) folder settings as JSON string.
     * Walks the ancestor chain from root to the given folder, merging at each level.
     */
    [[nodiscard]] std::string get_effective_folder_settings_json(const std::string& folder_path) const;

    /**
     * Get a fully resolved connection profile (folder chain + connection overrides)
     */
    [[nodiscard]] std::string get_effective_connection_profile_json(const std::string& connection_name) const;

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
    [[nodiscard]] std::optional<ConnectionProfile> get_connection(std::string_view name) const;
    
    /**
     * Get all connections as JSON string (for frontend)
     */
    [[nodiscard]] std::string get_connections_json() const;
    
    /**
     * Get list of all connection profiles
     */
    [[nodiscard]] const std::vector<ConnectionProfile>& get_connections() const { return m_connections; }

    // ========================================================================
    // Feed Account Management
    // ========================================================================

    /**
     * Add a new feed account
     */
    [[nodiscard]] bool add_feed_account(const FeedAccount& account);

    /**
     * Update an existing feed account
     */
    [[nodiscard]] bool update_feed_account(const FeedAccount& account);

    /**
     * Delete a feed account by id
     */
    [[nodiscard]] bool delete_feed_account(std::string_view id);

    /**
     * Clear cached tokens for all connections belonging to a feed account.
     * Also clears the account's refresh_token.
     */
    bool clear_tokens_for_account(std::string_view account_id);

    /**
     * Forget (fully remove) a feed account: clear its cached tokens,
     * delete all connections imported by it, and remove the account record.
     */
    bool forget_account(std::string_view account_id);

    /**
     * Get all feed accounts
     */
    [[nodiscard]] std::vector<FeedAccount> get_feed_accounts() const;

    /**
     * Get all feed accounts as JSON string
     */
    [[nodiscard]] std::string get_feed_accounts_json() const;
    
private:
    std::vector<ConnectionProfile> m_connections;
    std::vector<std::string> m_folders;
    std::unordered_map<std::string, FolderSettings> m_folder_settings;
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
    bool load_folder_settings();
    bool save_all_folder_settings();

    // Cascade resolution helpers
    static std::vector<std::string> get_ancestor_paths(const std::string& path);
    FolderSettings resolve_folder_settings(const std::string& folder_path) const;
    ConnectionProfile resolve_effective_profile(const ConnectionProfile& conn) const;
    bool load_legacy_json();
    bool load_settings();
    bool save_settings() const;
    void set_last_database_path(const std::string& path);
    std::string get_last_database_path() const;
};
