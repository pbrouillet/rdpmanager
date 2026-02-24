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
#include <cctype>
#include <sqlite3.h>
#include <set>
#include <chrono>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#endif

ConfigManager::ConfigManager() {
    m_config_path = get_config_file_path();
    m_settings_path = get_config_directory() / "settings.json";
    
    // Ensure config directory exists
    fs::path config_dir = get_config_directory();
    if (!fs::exists(config_dir)) {
        fs::create_directories(config_dir);
    }

    load_settings();

    std::string startup_db_path = get_last_database_path();
    if (startup_db_path.empty()) {
        startup_db_path = (config_dir / "connections.db").string();
    }

    if (!open_database_internal(startup_db_path)) {
        std::cerr << "[ConfigManager] Failed to open startup database: " << startup_db_path << std::endl;
        return;
    }

    set_last_database_path(startup_db_path);
    save_settings();

    // One-time migration path for legacy JSON store.
    if (m_connections.empty() && fs::exists(m_config_path)) {
        if (load_legacy_json()) {
            save();
        }
    }
}

ConfigManager::~ConfigManager() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
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
    p.folder     = json_utils::get_string(obj, "folder");
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
    json_object_set_new(obj, "folder",     json_string(c.folder.c_str()));
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

static std::string normalize_token_cache_key(const std::string& value) {
    std::string normalized = value;
    normalized.erase(normalized.begin(),
                     std::find_if(normalized.begin(), normalized.end(), [](unsigned char ch) {
                         return !std::isspace(ch);
                     }));
    normalized.erase(std::find_if(normalized.rbegin(), normalized.rend(), [](unsigned char ch) {
                         return !std::isspace(ch);
                     }).base(),
                     normalized.end());
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

// ============================================================================
// Load / Save
// ============================================================================

bool ConfigManager::load() {
    m_connections.clear();

    if (!m_db) {
        return true;
    }

    const char* sql = "SELECT profile_json FROM connections ORDER BY name COLLATE NOCASE";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to prepare SELECT statement: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* profile_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (!profile_json) {
            continue;
        }

        json_error_t error;
        json_t* obj = json_loads(profile_json, 0, &error);
        if (!obj || !json_is_object(obj)) {
            if (obj) {
                json_decref(obj);
            }
            continue;
        }

        ConnectionProfile profile = profile_from_json(obj);
        json_decref(obj);
        if (!profile.name.empty() && !profile.hostname.empty()) {
            m_connections.push_back(std::move(profile));
        }
    }

    sqlite3_finalize(stmt);
    std::cout << "[ConfigManager] Loaded " << m_connections.size() << " connections" << std::endl;
    return true;
}

bool ConfigManager::save() {
    if (!m_db) {
        return false;
    }

    if (sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to begin transaction: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    bool success = true;
    if (sqlite3_exec(m_db, "DELETE FROM connections", nullptr, nullptr, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to clear connections table: " << sqlite3_errmsg(m_db) << std::endl;
        success = false;
    }

    const char* upsert_sql =
        "INSERT INTO connections(name, profile_json) VALUES(?, ?) "
        "ON CONFLICT(name) DO UPDATE SET profile_json=excluded.profile_json";

    sqlite3_stmt* stmt = nullptr;
    if (success && sqlite3_prepare_v2(m_db, upsert_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        for (const auto& c : m_connections) {
            json_t* obj = profile_to_json(c);
            char* dump = json_dumps(obj, JSON_COMPACT);
            json_decref(obj);
            if (!dump) {
                success = false;
                break;
            }

            sqlite3_bind_text(stmt, 1, c.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, dump, -1, SQLITE_TRANSIENT);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "[ConfigManager] Upsert failed for " << c.name << ": "
                          << sqlite3_errmsg(m_db) << std::endl;
                free(dump);
                success = false;
                break;
            }

            free(dump);
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }
        sqlite3_finalize(stmt);
    } else if (success) {
        std::cerr << "[ConfigManager] Failed to prepare UPSERT statement: " << sqlite3_errmsg(m_db) << std::endl;
        success = false;
    }

    if (success) {
        if (sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            std::cerr << "[ConfigManager] Failed to commit transaction: " << sqlite3_errmsg(m_db) << std::endl;
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
    } else {
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }

    std::cout << "[ConfigManager] Saved " << m_connections.size() << " connections to " << m_database_path << std::endl;
    return true;
}

bool ConfigManager::save_connection(const ConnectionProfile& profile) {
    if (!m_db) {
        return false;
    }

    if (profile.name.empty() || profile.hostname.empty()) {
        return false;
    }

    // Check if connection with this name exists
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&profile](const ConnectionProfile& p) { return p.name == profile.name; });
    
    if (it != m_connections.end()) {
        // Update existing - preserve name, update everything else
        it->folder = profile.folder;
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

    const auto fresh = get_connection(profile.name);
    if (!fresh.has_value()) {
        return false;
    }

    json_t* obj = profile_to_json(fresh.value());
    char* dump = json_dumps(obj, JSON_COMPACT);
    json_decref(obj);
    if (!dump) {
        return false;
    }

    const char* upsert_sql =
        "INSERT INTO connections(name, profile_json) VALUES(?, ?) "
        "ON CONFLICT(name) DO UPDATE SET profile_json=excluded.profile_json";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, upsert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        free(dump);
        std::cerr << "[ConfigManager] Failed to prepare UPSERT statement: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, fresh->name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, dump, -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;

    if (!ok) {
        std::cerr << "[ConfigManager] Failed to save connection " << fresh->name << ": "
                  << sqlite3_errmsg(m_db) << std::endl;
    }

    sqlite3_finalize(stmt);
    free(dump);

    if (ok && !fresh->folder.empty()) {
        create_folder(fresh->folder);
    }

    return ok;
}

bool ConfigManager::delete_connection(const std::string& name) {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&name](const ConnectionProfile& p) { return p.name == name; });
    
    if (it == m_connections.end()) {
        return false;  // Not found
    }
    
    m_connections.erase(it);

    if (!m_db) {
        return false;
    }

    const char* sql = "DELETE FROM connections WHERE name = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to prepare DELETE statement: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        std::cerr << "[ConfigManager] Failed to delete connection " << name << ": "
                  << sqlite3_errmsg(m_db) << std::endl;
    }

    sqlite3_finalize(stmt);
    return ok;
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

bool ConfigManager::create_database(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    if (!open_database_internal(path)) {
        return false;
    }

    set_last_database_path(path);
    return save_settings();
}

bool ConfigManager::open_database(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    if (!open_database_internal(path)) {
        return false;
    }

    set_last_database_path(path);
    return save_settings();
}

bool ConfigManager::clone_database(const std::string& source_path, const std::string& target_path) {
    if (source_path.empty() || target_path.empty()) {
        return false;
    }

    fs::path source_fs = fs::path(source_path);
    fs::path target_fs = fs::path(target_path);

    if (source_fs == target_fs) {
        return false;
    }

    std::error_code ec;
    const fs::path target_parent = target_fs.parent_path();
    if (!target_parent.empty() && !fs::exists(target_parent)) {
        fs::create_directories(target_parent, ec);
        if (ec) {
            std::cerr << "[ConfigManager] Failed to create clone target directory: " << ec.message() << std::endl;
            return false;
        }
    }

    sqlite3* source_db = nullptr;
    if (sqlite3_open_v2(source_fs.string().c_str(), &source_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to open source database for clone: " << source_path << std::endl;
        if (source_db) {
            sqlite3_close(source_db);
        }
        return false;
    }

    sqlite3* target_db = nullptr;
    if (sqlite3_open(target_fs.string().c_str(), &target_db) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to open target database for clone: " << target_path << std::endl;
        sqlite3_close(source_db);
        if (target_db) {
            sqlite3_close(target_db);
        }
        return false;
    }

    sqlite3_backup* backup = sqlite3_backup_init(target_db, "main", source_db, "main");
    if (!backup) {
        std::cerr << "[ConfigManager] Failed to initialize sqlite backup: " << sqlite3_errmsg(target_db) << std::endl;
        sqlite3_close(target_db);
        sqlite3_close(source_db);
        return false;
    }

    const int step_result = sqlite3_backup_step(backup, -1);
    const int finish_result = sqlite3_backup_finish(backup);

    const bool copy_ok =
        (step_result == SQLITE_DONE || step_result == SQLITE_OK) &&
        (finish_result == SQLITE_OK);

    sqlite3_close(target_db);
    sqlite3_close(source_db);

    if (!copy_ok) {
        std::cerr << "[ConfigManager] Failed to clone database from " << source_path
                  << " to " << target_path << std::endl;
        return false;
    }

    if (!open_database_internal(target_fs)) {
        return false;
    }

    set_last_database_path(target_path);
    return save_settings();
}

bool ConfigManager::close_database() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }

    m_database_path.clear();
    m_connections.clear();
    set_last_database_path("");
    return save_settings();
}

bool ConfigManager::has_open_database() const {
    return m_db != nullptr;
}

std::string ConfigManager::get_database_path() const {
    return m_database_path.string();
}

bool ConfigManager::open_database_internal(const fs::path& path) {
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    const fs::path parent = path.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        fs::create_directories(parent, ec);
        if (ec) {
            std::cerr << "[ConfigManager] Failed to create database directory: " << ec.message() << std::endl;
            return false;
        }
    }

    sqlite3* new_db = nullptr;
    if (sqlite3_open(path.string().c_str(), &new_db) != SQLITE_OK) {
        std::cerr << "[ConfigManager] sqlite open failed for " << path << ": "
                  << sqlite3_errmsg(new_db) << std::endl;
        if (new_db) {
            sqlite3_close(new_db);
        }
        return false;
    }

    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }

    m_db = new_db;
    m_database_path = path;

    if (!ensure_schema()) {
        sqlite3_close(m_db);
        m_db = nullptr;
        m_database_path.clear();
        return false;
    }

    if (!load()) {
        return false;
    }

    return load_folders();
}

bool ConfigManager::ensure_schema() {
    if (!m_db) {
        return false;
    }

    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS connections (
            name TEXT PRIMARY KEY NOT NULL,
            profile_json TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS folders (
            path TEXT PRIMARY KEY NOT NULL
        );
        CREATE TABLE IF NOT EXISTS "token-cache" (
            hostname TEXT NOT NULL,
            cache_kind TEXT NOT NULL,
            access_token TEXT NOT NULL,
            expires_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            PRIMARY KEY(hostname, cache_kind)
        )
    )SQL";

    if (sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to ensure schema: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    return true;
}

bool ConfigManager::load_folders() {
    m_folders.clear();

    if (!m_db) {
        return true;
    }

    const char* sql = "SELECT path FROM folders ORDER BY path COLLATE NOCASE";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to prepare folder SELECT statement: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (path && path[0] != '\0') {
            m_folders.push_back(path);
        }
    }

    sqlite3_finalize(stmt);

    std::set<std::string> seen(m_folders.begin(), m_folders.end());
    for (const auto& conn : m_connections) {
        if (!conn.folder.empty() && seen.insert(conn.folder).second) {
            m_folders.push_back(conn.folder);
        }
    }

    std::sort(m_folders.begin(), m_folders.end());
    return true;
}

bool ConfigManager::save_folders() {
    if (!m_db) {
        return false;
    }

    if (sqlite3_exec(m_db, "DELETE FROM folders", nullptr, nullptr, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to clear folders table: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    const char* sql = "INSERT OR IGNORE INTO folders(path) VALUES(?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to prepare folder INSERT statement: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    bool ok = true;
    for (const auto& folder : m_folders) {
        if (folder.empty()) {
            continue;
        }

        sqlite3_bind_text(stmt, 1, folder.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "[ConfigManager] Failed to persist folder " << folder << ": "
                      << sqlite3_errmsg(m_db) << std::endl;
            ok = false;
            break;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    sqlite3_finalize(stmt);
    return ok;
}

bool ConfigManager::load_legacy_json() {
    if (!fs::exists(m_config_path)) {
        return true;
    }

    std::ifstream file(m_config_path);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();
    file.close();

    json_error_t error;
    json_t* root = json_loads(content.c_str(), 0, &error);
    if (!root || !json_is_array(root)) {
        if (root) {
            json_decref(root);
        }
        return false;
    }

    std::vector<ConnectionProfile> migrated;
    size_t index;
    json_t* value;
    json_array_foreach(root, index, value) {
        if (!json_is_object(value)) {
            continue;
        }

        ConnectionProfile profile = profile_from_json(value);
        if (!profile.name.empty() && !profile.hostname.empty()) {
            migrated.push_back(std::move(profile));
        }
    }
    json_decref(root);

    if (!migrated.empty()) {
        m_connections = std::move(migrated);
        std::cout << "[ConfigManager] Migrated " << m_connections.size()
                  << " legacy connections from " << m_config_path << std::endl;
    }

    return true;
}

bool ConfigManager::load_settings() {
    if (!fs::exists(m_settings_path)) {
        return true;
    }

    std::ifstream file(m_settings_path);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();
    file.close();

    json_error_t error;
    json_t* root = json_loads(content.c_str(), 0, &error);
    if (!root || !json_is_object(root)) {
        if (root) {
            json_decref(root);
        }
        return false;
    }

    m_last_database_path = json_utils::get_string(root, "last_database_path");
    json_decref(root);
    return true;
}

bool ConfigManager::save_settings() const {
    json_t* root = json_object();
    json_object_set_new(root, "last_database_path", json_string(m_last_database_path.c_str()));

    char* dump = json_dumps(root, JSON_INDENT(2) | JSON_SORT_KEYS);
    json_decref(root);
    if (!dump) {
        return false;
    }

    std::ofstream file(m_settings_path);
    if (!file.is_open()) {
        free(dump);
        return false;
    }

    file << dump << "\n";
    free(dump);
    return true;
}

void ConfigManager::set_last_database_path(const std::string& path) {
    m_last_database_path = path;
}

std::string ConfigManager::get_last_database_path() const {
    return m_last_database_path;
}

bool ConfigManager::create_folder(const std::string& folder) {
    if (!m_db || folder.empty()) {
        return false;
    }

    const char* sql = "INSERT OR IGNORE INTO folders(path) VALUES(?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to prepare folder INSERT statement: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, folder.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!ok) {
        std::cerr << "[ConfigManager] Failed to create folder " << folder << ": "
                  << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    if (std::find(m_folders.begin(), m_folders.end(), folder) == m_folders.end()) {
        m_folders.push_back(folder);
        std::sort(m_folders.begin(), m_folders.end());
    }

    return true;
}

bool ConfigManager::move_folder(const std::string& source_folder, const std::string& target_parent_folder) {
    if (!m_db || source_folder.empty()) {
        return false;
    }

    const auto source_it = std::find(m_folders.begin(), m_folders.end(), source_folder);
    if (source_it == m_folders.end()) {
        return false;
    }

    const std::size_t source_pos = source_folder.find_last_of('/');
    const std::string source_name =
        (source_pos == std::string::npos) ? source_folder : source_folder.substr(source_pos + 1);
    const std::string target_prefix = target_parent_folder.empty()
        ? source_name
        : (target_parent_folder + "/" + source_name);

    if (target_prefix == source_folder) {
        return true;
    }

    if (target_parent_folder == source_folder ||
        target_parent_folder.rfind(source_folder + "/", 0) == 0) {
        return false;
    }

    auto rewrite = [&](const std::string& path) -> std::string {
        if (path == source_folder) {
            return target_prefix;
        }
        if (path.rfind(source_folder + "/", 0) == 0) {
            return target_prefix + path.substr(source_folder.size());
        }
        return path;
    };

    for (auto& folder : m_folders) {
        folder = rewrite(folder);
    }

    std::sort(m_folders.begin(), m_folders.end());
    m_folders.erase(std::unique(m_folders.begin(), m_folders.end()), m_folders.end());

    for (auto& conn : m_connections) {
        conn.folder = rewrite(conn.folder);
    }

    if (!save()) {
        return false;
    }

    return save_folders();
}

bool ConfigManager::rename_folder(const std::string& source_folder, const std::string& new_name) {
    if (!m_db || source_folder.empty() || new_name.empty() || new_name.find('/') != std::string::npos) {
        return false;
    }

    const auto source_it = std::find(m_folders.begin(), m_folders.end(), source_folder);
    if (source_it == m_folders.end()) {
        return false;
    }

    const std::size_t source_pos = source_folder.find_last_of('/');
    const std::string parent =
        (source_pos == std::string::npos) ? "" : source_folder.substr(0, source_pos);
    const std::string target_prefix = parent.empty() ? new_name : (parent + "/" + new_name);

    if (target_prefix == source_folder) {
        return true;
    }

    for (const auto& existing : m_folders) {
        if (existing == target_prefix || existing.rfind(target_prefix + "/", 0) == 0) {
            return false;
        }
    }

    auto rewrite = [&](const std::string& path) -> std::string {
        if (path == source_folder) {
            return target_prefix;
        }
        if (path.rfind(source_folder + "/", 0) == 0) {
            return target_prefix + path.substr(source_folder.size());
        }
        return path;
    };

    for (auto& folder : m_folders) {
        folder = rewrite(folder);
    }

    std::sort(m_folders.begin(), m_folders.end());
    m_folders.erase(std::unique(m_folders.begin(), m_folders.end()), m_folders.end());

    for (auto& conn : m_connections) {
        conn.folder = rewrite(conn.folder);
    }

    if (!save()) {
        return false;
    }

    return save_folders();
}

bool ConfigManager::delete_folder(const std::string& folder) {
    if (!m_db || folder.empty()) {
        return false;
    }

    auto in_deleted_tree = [&](const std::string& path) -> bool {
        return path == folder || path.rfind(folder + "/", 0) == 0;
    };

    m_folders.erase(
        std::remove_if(m_folders.begin(), m_folders.end(),
            [&](const std::string& path) { return in_deleted_tree(path); }),
        m_folders.end());

    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(),
            [&](const ConnectionProfile& conn) { return in_deleted_tree(conn.folder); }),
        m_connections.end());

    if (!save()) {
        return false;
    }

    return save_folders();
}

std::string ConfigManager::get_folders_json() const {
    json_t* root = json_array();
    for (const auto& folder : m_folders) {
        json_array_append_new(root, json_string(folder.c_str()));
    }

    char* dump = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    std::string result(dump ? dump : "[]");
    free(dump);
    return result;
}

std::optional<std::string> ConfigManager::get_cached_token(const std::string& hostname,
                                                           const std::string& cache_kind) const {
    if (!m_db) {
        return std::nullopt;
    }

    const std::string host_key = normalize_token_cache_key(hostname);
    const std::string kind_key = normalize_token_cache_key(cache_kind);
    if (host_key.empty() || kind_key.empty()) {
        return std::nullopt;
    }

    const char* sql =
        "SELECT access_token, expires_at FROM \"token-cache\" WHERE hostname = ? AND cache_kind = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to prepare token cache SELECT: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, host_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, kind_key.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<std::string> result;
    const auto now = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* token = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const int64_t expires_at = sqlite3_column_int64(stmt, 1);

        if (token && expires_at > (now + 30)) {
            result = std::string(token);
        } else {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

bool ConfigManager::set_cached_token(const std::string& hostname,
                                     const std::string& cache_kind,
                                     const std::string& token,
                                     int64_t expires_at_epoch) {
    if (!m_db) {
        return false;
    }

    const std::string host_key = normalize_token_cache_key(hostname);
    const std::string kind_key = normalize_token_cache_key(cache_kind);
    if (host_key.empty() || kind_key.empty() || token.empty() || expires_at_epoch <= 0) {
        return false;
    }

    const auto now = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    const char* sql =
        "INSERT INTO \"token-cache\"(hostname, cache_kind, access_token, expires_at, updated_at) "
        "VALUES(?, ?, ?, ?, ?) "
        "ON CONFLICT(hostname, cache_kind) DO UPDATE SET "
        "access_token=excluded.access_token, expires_at=excluded.expires_at, updated_at=excluded.updated_at";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to prepare token cache UPSERT: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, host_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, kind_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, expires_at_epoch);
    sqlite3_bind_int64(stmt, 5, now);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        std::cerr << "[ConfigManager] Failed to save token cache entry: "
                  << sqlite3_errmsg(m_db) << std::endl;
    }

    sqlite3_finalize(stmt);
    return ok;
}

bool ConfigManager::delete_cached_token(const std::string& hostname,
                                        const std::string& cache_kind) {
    if (!m_db) {
        return false;
    }

    const std::string host_key = normalize_token_cache_key(hostname);
    const std::string kind_key = normalize_token_cache_key(cache_kind);
    if (host_key.empty() || kind_key.empty()) {
        return false;
    }

    const char* sql = "DELETE FROM \"token-cache\" WHERE hostname = ? AND cache_kind = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[ConfigManager] Failed to prepare token cache DELETE: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, host_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, kind_key.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}
