/**
 * Configuration Manager Implementation
 * 
 * Uses jansson for JSON serialization/deserialization of connection profiles.
 */

#include "config_manager.hpp"
#include "json_utils.hpp"
#include "sqlite_helpers.hpp"

#include "logger.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <sqlite3.h>
#include <set>
#include <chrono>
#include <format>
#include <unordered_map>

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
        LOG_ERROR("ConfigMgr", "Failed to open startup database: " << startup_db_path);
        return;
    }

    set_last_database_path(startup_db_path);
    save_settings();

    // One-time migration path for legacy JSON store.
    if (m_connections.empty() && fs::exists(m_config_path)) {
        if (load_legacy_json()) {
            (void)save();
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
    p.source_account_id = json_utils::get_string(obj, "source_account_id");

    // Inheritance tracking
    json_t* overrides_arr = json_object_get(obj, "overridden_fields");
    if (overrides_arr && json_is_array(overrides_arr)) {
        p.legacy_profile = false;
        size_t idx;
        json_t* val;
        json_array_foreach(overrides_arr, idx, val) {
            if (json_is_string(val)) {
                p.overridden_fields.insert(json_string_value(val));
            }
        }
    } else {
        // Old profile without overridden_fields: treat all inheritable fields as overridden
        p.legacy_profile = true;
        for (const auto& name : inheritable_field_names()) {
            p.overridden_fields.insert(name);
        }
    }

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
    json_object_set_new(obj, "source_account_id", json_string(c.source_account_id.c_str()));

    // Inheritance tracking
    json_t* overrides_arr = json_array();
    for (const auto& field : c.overridden_fields) {
        json_array_append_new(overrides_arr, json_string(field.c_str()));
    }
    json_object_set_new(obj, "overridden_fields", overrides_arr);

    return obj;
}

// ============================================================================
// Helper: serialize/deserialize FolderSettings to/from JSON
// ============================================================================
static json_t* folder_settings_to_json(const FolderSettings& s) {
    json_t* obj = json_object();
    auto set_opt_bool = [&](const char* key, const std::optional<bool>& val) {
        if (val.has_value()) json_object_set_new(obj, key, json_boolean(*val));
    };
    auto set_opt_int = [&](const char* key, const std::optional<int>& val) {
        if (val.has_value()) json_object_set_new(obj, key, json_integer(*val));
    };
    auto set_opt_str = [&](const char* key, const std::optional<std::string>& val) {
        if (val.has_value()) json_object_set_new(obj, key, json_string(val->c_str()));
    };

    set_opt_bool("home_drive", s.home_drive);
    set_opt_bool("clipboard", s.clipboard);
    set_opt_bool("cert_tofu", s.cert_tofu);
    set_opt_bool("usb_auto", s.usb_auto);
    set_opt_bool("floatbar", s.floatbar);
    set_opt_bool("dynamic_resolution", s.dynamic_resolution);
    set_opt_bool("network_auto", s.network_auto);
    set_opt_bool("gfx_avc420", s.gfx_avc420);
    set_opt_bool("compression", s.compression);
    set_opt_bool("audio_pulse", s.audio_pulse);
    set_opt_bool("prevent_session_lock", s.prevent_session_lock);
    set_opt_bool("auto_reconnect", s.auto_reconnect);
    set_opt_int("auto_reconnect_max_retries", s.auto_reconnect_max_retries);
    set_opt_str("gateway_hostname", s.gateway_hostname);
    set_opt_bool("enable_rds_aad_auth", s.enable_rds_aad_auth);
    set_opt_bool("target_is_aad_joined", s.target_is_aad_joined);
    set_opt_str("load_balance_info", s.load_balance_info);
    return obj;
}

static FolderSettings folder_settings_from_json(json_t* obj) {
    FolderSettings s;
    if (!obj || !json_is_object(obj)) return s;

    auto get_opt_bool = [&](const char* key) -> std::optional<bool> {
        json_t* val = json_object_get(obj, key);
        if (val && json_is_boolean(val)) return json_is_true(val);
        return std::nullopt;
    };
    auto get_opt_int = [&](const char* key) -> std::optional<int> {
        json_t* val = json_object_get(obj, key);
        if (val && json_is_integer(val)) return static_cast<int>(json_integer_value(val));
        return std::nullopt;
    };
    auto get_opt_str = [&](const char* key) -> std::optional<std::string> {
        json_t* val = json_object_get(obj, key);
        if (val && json_is_string(val)) return std::string(json_string_value(val));
        return std::nullopt;
    };

    s.home_drive = get_opt_bool("home_drive");
    s.clipboard = get_opt_bool("clipboard");
    s.cert_tofu = get_opt_bool("cert_tofu");
    s.usb_auto = get_opt_bool("usb_auto");
    s.floatbar = get_opt_bool("floatbar");
    s.dynamic_resolution = get_opt_bool("dynamic_resolution");
    s.network_auto = get_opt_bool("network_auto");
    s.gfx_avc420 = get_opt_bool("gfx_avc420");
    s.compression = get_opt_bool("compression");
    s.audio_pulse = get_opt_bool("audio_pulse");
    s.prevent_session_lock = get_opt_bool("prevent_session_lock");
    s.auto_reconnect = get_opt_bool("auto_reconnect");
    s.auto_reconnect_max_retries = get_opt_int("auto_reconnect_max_retries");
    s.gateway_hostname = get_opt_str("gateway_hostname");
    s.enable_rds_aad_auth = get_opt_bool("enable_rds_aad_auth");
    s.target_is_aad_joined = get_opt_bool("target_is_aad_joined");
    s.load_balance_info = get_opt_str("load_balance_info");
    return s;
}

/// Merge layers: overlay `top` onto `base`. Fields set in `top` override `base`.
static FolderSettings merge_folder_settings(const FolderSettings& base, const FolderSettings& top) {
    FolderSettings merged = base;
    auto merge = [](auto& dst, const auto& src) { if (src.has_value()) dst = src; };
    merge(merged.home_drive, top.home_drive);
    merge(merged.clipboard, top.clipboard);
    merge(merged.cert_tofu, top.cert_tofu);
    merge(merged.usb_auto, top.usb_auto);
    merge(merged.floatbar, top.floatbar);
    merge(merged.dynamic_resolution, top.dynamic_resolution);
    merge(merged.network_auto, top.network_auto);
    merge(merged.gfx_avc420, top.gfx_avc420);
    merge(merged.compression, top.compression);
    merge(merged.audio_pulse, top.audio_pulse);
    merge(merged.prevent_session_lock, top.prevent_session_lock);
    merge(merged.auto_reconnect, top.auto_reconnect);
    merge(merged.auto_reconnect_max_retries, top.auto_reconnect_max_retries);
    merge(merged.gateway_hostname, top.gateway_hostname);
    merge(merged.enable_rds_aad_auth, top.enable_rds_aad_auth);
    merge(merged.target_is_aad_joined, top.target_is_aad_joined);
    merge(merged.load_balance_info, top.load_balance_info);
    return merged;
}

static std::string normalize_token_cache_key(std::string_view value) {
    std::string normalized{value};
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
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare SELECT statement: "
                  << sqlite3_errmsg(m_db));
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
            if (obj) json_decref(obj);
            continue;
        }

        auto profile = profile_from_json(obj);
        json_decref(obj);
        if (!profile.name.empty() && !profile.hostname.empty()) {
            m_connections.push_back(std::move(profile));
        }
    }

    LOG_INFO("ConfigMgr", "Loaded " << m_connections.size() << " connections");
    return true;
}

bool ConfigManager::save() {
    if (!m_db) {
        return false;
    }

    if (sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOG_ERROR("ConfigMgr", "Failed to begin transaction: " << sqlite3_errmsg(m_db));
        return false;
    }

    bool success = true;
    if (sqlite3_exec(m_db, "DELETE FROM connections", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOG_ERROR("ConfigMgr", "Failed to clear connections table: " << sqlite3_errmsg(m_db));
        success = false;
    }

    const char* upsert_sql =
        "INSERT INTO connections(name, profile_json) VALUES(?, ?) "
        "ON CONFLICT(name) DO UPDATE SET profile_json=excluded.profile_json";

    sqlite3_stmt* stmt = nullptr;
    if (success && sqlite3_prepare_v2(m_db, upsert_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        for (const auto& c : m_connections) {
            json_utils::JsonPtr obj{profile_to_json(c)};
            json_utils::MallocPtr dump{json_dumps(obj.get(), JSON_COMPACT)};
            if (!dump) {
                success = false;
                break;
            }

            sqlite3_bind_text(stmt, 1, c.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, dump.get(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                LOG_ERROR("ConfigMgr", "Upsert failed for " << c.name << ": "
                          << sqlite3_errmsg(m_db));
                success = false;
                break;
            }

            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }
        sqlite3_finalize(stmt);
    } else if (success) {
        LOG_ERROR("ConfigMgr", "Failed to prepare UPSERT statement: " << sqlite3_errmsg(m_db));
        success = false;
    }

    if (success) {
        if (sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            LOG_ERROR("ConfigMgr", "Failed to commit transaction: " << sqlite3_errmsg(m_db));
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
    } else {
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }

    LOG_INFO("ConfigMgr", "Saved " << m_connections.size() << " connections to " << m_database_path);
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
        // Update existing - preserve the name, replace all other fields
        std::string saved_name = it->name;
        *it = profile;
        it->name = saved_name;
        if (it->port <= 0) it->port = 3389;
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

    json_utils::JsonPtr obj{profile_to_json(fresh.value())};
    json_utils::MallocPtr dump{json_dumps(obj.get(), JSON_COMPACT)};
    if (!dump) {
        return false;
    }

    const char* upsert_sql =
        "INSERT INTO connections(name, profile_json) VALUES(?, ?) "
        "ON CONFLICT(name) DO UPDATE SET profile_json=excluded.profile_json";
    SqliteStmt stmt(m_db, upsert_sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare UPSERT statement: " << sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, fresh->name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, dump.get(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;

    if (!ok) {
        LOG_ERROR("ConfigMgr", "Failed to save connection " << fresh->name << ": "
                  << sqlite3_errmsg(m_db));
    }

    if (ok && !fresh->folder.empty()) {
        (void)create_folder(fresh->folder);
    }

    return ok;
}

bool ConfigManager::delete_connection(std::string_view name) {
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
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare DELETE statement: " << sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, std::string{name}.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        LOG_ERROR("ConfigMgr", "Failed to delete connection " << name << ": "
                  << sqlite3_errmsg(m_db));
    }

    return ok;
}

std::optional<ConnectionProfile> ConfigManager::get_connection(std::string_view name) const {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&name](const ConnectionProfile& p) { return p.name == name; });
    
    if (it != m_connections.end()) {
        return *it;
    }
    return std::nullopt;
}

std::string ConfigManager::get_connections_json() const {
    json_utils::JsonPtr root{json_array()};
    for (const auto& c : m_connections) {
        json_array_append_new(root.get(), profile_to_json(c));
    }
    
    json_utils::MallocPtr dump{json_dumps(root.get(), JSON_COMPACT)};
    return std::string(dump ? dump.get() : "[]");
}

bool ConfigManager::create_database(std::string_view path) {
    if (path.empty()) {
        return false;
    }

    if (!open_database_internal(std::string{path})) {
        return false;
    }

    set_last_database_path(std::string{path});
    return save_settings();
}

bool ConfigManager::open_database(std::string_view path) {
    if (path.empty()) {
        return false;
    }

    if (!open_database_internal(std::string{path})) {
        return false;
    }

    set_last_database_path(std::string{path});
    return save_settings();
}

bool ConfigManager::clone_database(std::string_view source_path, std::string_view target_path) {
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
            LOG_ERROR("ConfigMgr", "Failed to create clone target directory: " << ec.message());
            return false;
        }
    }

    sqlite3* source_db = nullptr;
    if (sqlite3_open_v2(source_fs.string().c_str(), &source_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        LOG_ERROR("ConfigMgr", "Failed to open source database for clone: " << source_path);
        if (source_db) {
            sqlite3_close(source_db);
        }
        return false;
    }

    sqlite3* target_db = nullptr;
    if (sqlite3_open(target_fs.string().c_str(), &target_db) != SQLITE_OK) {
        LOG_ERROR("ConfigMgr", "Failed to open target database for clone: " << target_path);
        sqlite3_close(source_db);
        if (target_db) {
            sqlite3_close(target_db);
        }
        return false;
    }

    sqlite3_backup* backup = sqlite3_backup_init(target_db, "main", source_db, "main");
    if (!backup) {
        LOG_ERROR("ConfigMgr", "Failed to initialize sqlite backup: " << sqlite3_errmsg(target_db));
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
        LOG_ERROR("ConfigMgr", "Failed to clone database from " << source_path
                  << " to " << target_path);
        return false;
    }

    if (!open_database_internal(target_fs)) {
        return false;
    }

    set_last_database_path(std::string{target_path});
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
            LOG_ERROR("ConfigMgr", "Failed to create database directory: " << ec.message());
            return false;
        }
    }

    sqlite3* new_db = nullptr;
    if (sqlite3_open(path.string().c_str(), &new_db) != SQLITE_OK) {
        LOG_ERROR("ConfigMgr", "sqlite open failed for " << path << ": "
                  << sqlite3_errmsg(new_db));
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

    if (!load_folders()) {
        return false;
    }

    return load_folder_settings();
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
        );
        CREATE TABLE IF NOT EXISTS feed_accounts (
            id TEXT PRIMARY KEY NOT NULL,
            display_name TEXT NOT NULL DEFAULT '',
            email TEXT NOT NULL DEFAULT '',
            refresh_token TEXT NOT NULL DEFAULT '',
            last_synced INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS folder_settings (
            path TEXT PRIMARY KEY NOT NULL,
            settings_json TEXT NOT NULL DEFAULT '{}'
        )
    )SQL";

    if (sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOG_ERROR("ConfigMgr", "Failed to ensure schema: " << sqlite3_errmsg(m_db));
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
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare folder SELECT statement: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (path && path[0] != '\0') {
            m_folders.push_back(path);
        }
    }

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
        LOG_ERROR("ConfigMgr", "Failed to clear folders table: " << sqlite3_errmsg(m_db));
        return false;
    }

    const char* sql = "INSERT OR IGNORE INTO folders(path) VALUES(?)";
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare folder INSERT statement: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    bool ok = true;
    for (const auto& folder : m_folders) {
        if (folder.empty()) {
            continue;
        }

        sqlite3_bind_text(stmt, 1, folder.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOG_ERROR("ConfigMgr", "Failed to persist folder " << folder << ": "
                      << sqlite3_errmsg(m_db));
            ok = false;
            break;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    return ok;
}

// ============================================================================
// Folder Settings persistence
// ============================================================================

bool ConfigManager::load_folder_settings() {
    m_folder_settings.clear();

    if (!m_db) {
        return true;
    }

    const char* sql = "SELECT path, settings_json FROM folder_settings";
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare folder_settings SELECT: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* json_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (!path || !json_str) continue;

        json_error_t error;
        json_utils::JsonPtr root{json_loads(json_str, 0, &error)};
        if (root) {
            m_folder_settings[path] = folder_settings_from_json(root.get());
        }
    }

    LOG_INFO("ConfigMgr", "Loaded folder settings for " << m_folder_settings.size() << " folders");
    return true;
}

bool ConfigManager::save_all_folder_settings() {
    if (!m_db) {
        return false;
    }

    if (sqlite3_exec(m_db, "DELETE FROM folder_settings", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOG_ERROR("ConfigMgr", "Failed to clear folder_settings table: " << sqlite3_errmsg(m_db));
        return false;
    }

    const char* sql = "INSERT INTO folder_settings(path, settings_json) VALUES(?, ?)";
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare folder_settings INSERT: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    for (const auto& [path, settings] : m_folder_settings) {
        json_utils::JsonPtr obj{folder_settings_to_json(settings)};
        json_utils::MallocPtr dump{json_dumps(obj.get(), JSON_COMPACT)};
        if (!dump) continue;

        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, dump.get(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOG_ERROR("ConfigMgr", "Failed to persist folder_settings for " << path);
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    return true;
}

bool ConfigManager::save_folder_settings(const std::string& folder_path, const FolderSettings& settings) {
    if (!m_db) {
        return false;
    }

    m_folder_settings[folder_path] = settings;

    const char* sql = "INSERT INTO folder_settings(path, settings_json) VALUES(?, ?) "
                      "ON CONFLICT(path) DO UPDATE SET settings_json=excluded.settings_json";
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare folder_settings UPSERT: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    json_utils::JsonPtr obj{folder_settings_to_json(settings)};
    json_utils::MallocPtr dump{json_dumps(obj.get(), JSON_COMPACT)};
    if (!dump) return false;

    sqlite3_bind_text(stmt, 1, folder_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, dump.get(), -1, SQLITE_TRANSIENT);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        LOG_ERROR("ConfigMgr", "Failed to save folder_settings for " << folder_path);
    }
    return ok;
}

FolderSettings ConfigManager::get_folder_settings(const std::string& folder_path) const {
    auto it = m_folder_settings.find(folder_path);
    if (it != m_folder_settings.end()) {
        return it->second;
    }
    return {};
}

std::string ConfigManager::get_folder_settings_json(const std::string& folder_path) const {
    FolderSettings settings = get_folder_settings(folder_path);
    json_utils::JsonPtr obj{folder_settings_to_json(settings)};
    json_utils::MallocPtr dump{json_dumps(obj.get(), JSON_COMPACT)};
    return std::string(dump ? dump.get() : "{}");
}

// ============================================================================
// Cascade resolution
// ============================================================================

std::vector<std::string> ConfigManager::get_ancestor_paths(const std::string& path) {
    std::vector<std::string> ancestors;
    ancestors.push_back("");  // root level (global defaults)
    if (path.empty()) return ancestors;

    std::string current;
    size_t start = 0;
    while (start < path.size()) {
        size_t pos = path.find('/', start);
        if (pos == std::string::npos) {
            current = path;
            ancestors.push_back(current);
            break;
        }
        current = path.substr(0, pos);
        ancestors.push_back(current);
        start = pos + 1;
    }
    return ancestors;
}

FolderSettings ConfigManager::resolve_folder_settings(const std::string& folder_path) const {
    auto ancestors = get_ancestor_paths(folder_path);

    FolderSettings resolved;
    for (const auto& ancestor : ancestors) {
        auto it = m_folder_settings.find(ancestor);
        if (it != m_folder_settings.end()) {
            resolved = merge_folder_settings(resolved, it->second);
        }
    }
    return resolved;
}

std::string ConfigManager::get_effective_folder_settings_json(const std::string& folder_path) const {
    FolderSettings resolved = resolve_folder_settings(folder_path);
    json_utils::JsonPtr obj{folder_settings_to_json(resolved)};
    json_utils::MallocPtr dump{json_dumps(obj.get(), JSON_COMPACT)};
    return std::string(dump ? dump.get() : "{}");
}

ConnectionProfile ConfigManager::resolve_effective_profile(const ConnectionProfile& conn) const {
    ConnectionProfile result = conn;

    // Resolve folder chain settings
    FolderSettings folder_resolved = resolve_folder_settings(conn.folder);

    // Apply folder settings to connection for fields NOT overridden by the connection
    auto apply_bool = [&](const char* name, bool& field, const std::optional<bool>& folder_val) {
        if (conn.overridden_fields.count(name) == 0 && folder_val.has_value()) {
            field = *folder_val;
        }
    };
    auto apply_int = [&](const char* name, int& field, const std::optional<int>& folder_val) {
        if (conn.overridden_fields.count(name) == 0 && folder_val.has_value()) {
            field = *folder_val;
        }
    };
    auto apply_str = [&](const char* name, std::string& field, const std::optional<std::string>& folder_val) {
        if (conn.overridden_fields.count(name) == 0 && folder_val.has_value()) {
            field = *folder_val;
        }
    };

    apply_bool("home_drive", result.home_drive, folder_resolved.home_drive);
    apply_bool("clipboard", result.clipboard, folder_resolved.clipboard);
    apply_bool("cert_tofu", result.cert_tofu, folder_resolved.cert_tofu);
    apply_bool("usb_auto", result.usb_auto, folder_resolved.usb_auto);
    apply_bool("floatbar", result.floatbar, folder_resolved.floatbar);
    apply_bool("dynamic_resolution", result.dynamic_resolution, folder_resolved.dynamic_resolution);
    apply_bool("network_auto", result.network_auto, folder_resolved.network_auto);
    apply_bool("gfx_avc420", result.gfx_avc420, folder_resolved.gfx_avc420);
    apply_bool("compression", result.compression, folder_resolved.compression);
    apply_bool("audio_pulse", result.audio_pulse, folder_resolved.audio_pulse);
    apply_bool("prevent_session_lock", result.prevent_session_lock, folder_resolved.prevent_session_lock);
    apply_bool("auto_reconnect", result.auto_reconnect, folder_resolved.auto_reconnect);
    apply_int("auto_reconnect_max_retries", result.auto_reconnect_max_retries, folder_resolved.auto_reconnect_max_retries);
    apply_str("gateway_hostname", result.gateway_hostname, folder_resolved.gateway_hostname);
    apply_bool("enable_rds_aad_auth", result.enable_rds_aad_auth, folder_resolved.enable_rds_aad_auth);
    apply_bool("target_is_aad_joined", result.target_is_aad_joined, folder_resolved.target_is_aad_joined);
    apply_str("load_balance_info", result.load_balance_info, folder_resolved.load_balance_info);

    return result;
}

std::string ConfigManager::get_effective_connection_profile_json(const std::string& connection_name) const {
    auto conn_opt = get_connection(connection_name);
    if (!conn_opt.has_value()) {
        return "{}";
    }

    ConnectionProfile resolved = resolve_effective_profile(*conn_opt);
    json_utils::JsonPtr obj{profile_to_json(resolved)};
    json_utils::MallocPtr dump{json_dumps(obj.get(), JSON_COMPACT)};
    return std::string(dump ? dump.get() : "{}");
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
    json_utils::JsonPtr root{json_loads(content.c_str(), 0, &error)};
    if (!root || !json_is_array(root.get())) {
        return false;
    }

    std::vector<ConnectionProfile> migrated;
    size_t index;
    json_t* value;
    json_array_foreach(root.get(), index, value) {
        if (!json_is_object(value)) {
            continue;
        }

        ConnectionProfile profile = profile_from_json(value);
        if (!profile.name.empty() && !profile.hostname.empty()) {
            migrated.push_back(std::move(profile));
        }
    }

    if (!migrated.empty()) {
        m_connections = std::move(migrated);
        LOG_INFO("ConfigMgr", "Migrated " << m_connections.size()
                  << " legacy connections from " << m_config_path);
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
    json_utils::JsonPtr root{json_loads(content.c_str(), 0, &error)};
    if (!root || !json_is_object(root.get())) {
        return false;
    }

    m_last_database_path = json_utils::get_string(root.get(), "last_database_path");
    return true;
}

bool ConfigManager::save_settings() const {
    json_utils::JsonPtr root{json_object()};
    json_object_set_new(root.get(), "last_database_path", json_string(m_last_database_path.c_str()));

    json_utils::MallocPtr dump{json_dumps(root.get(), JSON_INDENT(2) | JSON_SORT_KEYS)};
    if (!dump) {
        return false;
    }

    std::ofstream file(m_settings_path);
    if (!file.is_open()) {
        return false;
    }

    file << dump.get() << "\n";
    return true;
}

void ConfigManager::set_last_database_path(const std::string& path) {
    m_last_database_path = path;
}

std::string ConfigManager::get_last_database_path() const {
    return m_last_database_path;
}

bool ConfigManager::create_folder(std::string_view folder) {
    if (!m_db || folder.empty()) {
        return false;
    }

    const char* sql = "INSERT OR IGNORE INTO folders(path) VALUES(?)";
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare folder INSERT statement: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, std::string{folder}.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;

    if (!ok) {
        LOG_ERROR("ConfigMgr", "Failed to create folder " << folder << ": "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    auto folder_str = std::string{folder};
    if (std::find(m_folders.begin(), m_folders.end(), folder_str) == m_folders.end()) {
        m_folders.push_back(std::move(folder_str));
        std::sort(m_folders.begin(), m_folders.end());
    }

    return true;
}

bool ConfigManager::move_folder(std::string_view source_folder, std::string_view target_parent_folder) {
    if (!m_db || source_folder.empty()) {
        return false;
    }

    auto source_str = std::string{source_folder};
    const auto source_it = std::find(m_folders.begin(), m_folders.end(), source_str);
    if (source_it == m_folders.end()) {
        return false;
    }

    const auto source_pos = source_str.find_last_of('/');
    const auto source_name =
        (source_pos == std::string::npos) ? source_str : source_str.substr(source_pos + 1);
    const auto target_parent_str = std::string{target_parent_folder};
    const auto target_prefix = target_parent_str.empty()
        ? source_name
        : (target_parent_str + "/" + source_name);

    if (target_prefix == source_str) {
        return true;
    }

    if (target_parent_str == source_str ||
        target_parent_str.rfind(source_str + "/", 0) == 0) {
        return false;
    }

    auto rewrite = [&](const std::string& path) -> std::string {
        if (path == source_str) {
            return target_prefix;
        }
        if (path.rfind(source_str + "/", 0) == 0) {
            return target_prefix + path.substr(source_str.size());
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

    // Rewrite folder_settings paths
    std::unordered_map<std::string, FolderSettings> new_settings;
    for (auto& [path, settings] : m_folder_settings) {
        new_settings[rewrite(path)] = std::move(settings);
    }
    m_folder_settings = std::move(new_settings);

    if (!save()) {
        return false;
    }
    if (!save_folders()) {
        return false;
    }
    return save_all_folder_settings();
}

bool ConfigManager::rename_folder(std::string_view source_folder, std::string_view new_name) {
    if (!m_db || source_folder.empty() || new_name.empty() || new_name.find('/') != std::string_view::npos) {
        return false;
    }

    auto source_str = std::string{source_folder};
    const auto source_it = std::find(m_folders.begin(), m_folders.end(), source_str);
    if (source_it == m_folders.end()) {
        return false;
    }

    const auto source_pos = source_str.find_last_of('/');
    const auto parent =
        (source_pos == std::string::npos) ? "" : source_str.substr(0, source_pos);
    const auto target_prefix = parent.empty() ? std::string{new_name} : (parent + "/" + std::string{new_name});

    if (target_prefix == source_str) {
        return true;
    }

    for (const auto& existing : m_folders) {
        if (existing == target_prefix || existing.rfind(target_prefix + "/", 0) == 0) {
            return false;
        }
    }

    auto rewrite = [&](const std::string& path) -> std::string {
        if (path == source_str) {
            return target_prefix;
        }
        if (path.rfind(source_str + "/", 0) == 0) {
            return target_prefix + path.substr(source_str.size());
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

    // Rewrite folder_settings paths
    std::unordered_map<std::string, FolderSettings> new_settings;
    for (auto& [path, settings] : m_folder_settings) {
        new_settings[rewrite(path)] = std::move(settings);
    }
    m_folder_settings = std::move(new_settings);

    if (!save()) {
        return false;
    }
    if (!save_folders()) {
        return false;
    }
    return save_all_folder_settings();
}

bool ConfigManager::delete_folder(std::string_view folder) {
    if (!m_db || folder.empty()) {
        return false;
    }

    auto folder_str = std::string{folder};
    auto in_deleted_tree = [&](const std::string& path) -> bool {
        return path == folder_str || path.rfind(folder_str + "/", 0) == 0;
    };

    m_folders.erase(
        std::remove_if(m_folders.begin(), m_folders.end(),
            [&](const std::string& path) { return in_deleted_tree(path); }),
        m_folders.end());

    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(),
            [&](const ConnectionProfile& conn) { return in_deleted_tree(conn.folder); }),
        m_connections.end());

    // Delete folder_settings for deleted folders
    for (auto it = m_folder_settings.begin(); it != m_folder_settings.end(); ) {
        if (in_deleted_tree(it->first)) {
            it = m_folder_settings.erase(it);
        } else {
            ++it;
        }
    }

    if (!save()) {
        return false;
    }
    if (!save_folders()) {
        return false;
    }
    return save_all_folder_settings();
}

std::string ConfigManager::get_folders_json() const {
    json_utils::JsonPtr root{json_array()};
    for (const auto& folder : m_folders) {
        json_array_append_new(root.get(), json_string(folder.c_str()));
    }

    json_utils::MallocPtr dump{json_dumps(root.get(), JSON_COMPACT)};
    return std::string(dump ? dump.get() : "[]");
}

std::optional<std::string> ConfigManager::get_cached_token(const std::string& hostname,
                                                           const std::string& cache_kind) const {
    LOG_INFO("TokenCache", "LOOKUP host=" << hostname << " kind=" << cache_kind);

    if (!m_db) {
        LOG_INFO("TokenCache", "LOOKUP MISS (no database open)");
        return std::nullopt;
    }

    const std::string host_key = normalize_token_cache_key(hostname);
    const std::string kind_key = normalize_token_cache_key(cache_kind);
    if (host_key.empty() || kind_key.empty()) {
        LOG_INFO("TokenCache", "LOOKUP MISS (empty key after normalization)");
        return std::nullopt;
    }

    const char* sql =
        "SELECT access_token, expires_at FROM \"token-cache\" WHERE hostname = ? AND cache_kind = ?";
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("TokenCache", "LOOKUP ERROR preparing SELECT: "
                  << sqlite3_errmsg(m_db));
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
            LOG_INFO("TokenCache", "LOOKUP HIT host=" << host_key
                      << " kind=" << kind_key
                      << " ttl=" << (expires_at - now) << "s"
                      << " len=" << result->size());
#ifdef VERBOSE_SECRETS
            LOG_DEBUG("TokenCache", "LOOKUP HIT token=" << result->substr(0, 80) << "...");
#endif
        } else {
            LOG_INFO("TokenCache", "LOOKUP EXPIRED host=" << host_key
                      << " kind=" << kind_key
                      << " expired_ago=" << (now - expires_at) << "s");
            return std::nullopt;
        }
    } else {
        LOG_INFO("TokenCache", "LOOKUP MISS host=" << host_key
                  << " kind=" << kind_key << " (no row)");
    }

    return result;
}

bool ConfigManager::set_cached_token(const std::string& hostname,
                                     const std::string& cache_kind,
                                     const std::string& token,
                                     int64_t expires_at_epoch) {
    LOG_INFO("TokenCache", "STORE host=" << hostname << " kind=" << cache_kind
              << " expires_at=" << expires_at_epoch << " len=" << token.size());
#ifdef VERBOSE_SECRETS
    LOG_DEBUG("TokenCache", "STORE token=" << token.substr(0, 80) << "...");
#endif

    if (!m_db) {
        LOG_ERROR("TokenCache", "STORE FAILED (no database open)");
        return false;
    }

    const std::string host_key = normalize_token_cache_key(hostname);
    const std::string kind_key = normalize_token_cache_key(cache_kind);
    if (host_key.empty() || kind_key.empty() || token.empty() || expires_at_epoch <= 0) {
        LOG_ERROR("TokenCache", "STORE FAILED (invalid parameters)");
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

    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("TokenCache", "STORE ERROR preparing UPSERT: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, host_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, kind_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, expires_at_epoch);
    sqlite3_bind_int64(stmt, 5, now);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (ok) {
        LOG_INFO("TokenCache", "STORE OK host=" << host_key << " kind=" << kind_key
                  << " ttl=" << (expires_at_epoch - now) << "s");
    } else {
        LOG_ERROR("TokenCache", "STORE FAILED host=" << host_key << " kind=" << kind_key
                  << ": " << sqlite3_errmsg(m_db));
    }

    return ok;
}

bool ConfigManager::delete_cached_token(const std::string& hostname,
                                        const std::string& cache_kind) {
    LOG_INFO("TokenCache", "DELETE host=" << hostname << " kind=" << cache_kind);

    if (!m_db) {
        LOG_ERROR("TokenCache", "DELETE FAILED (no database open)");
        return false;
    }

    const std::string host_key = normalize_token_cache_key(hostname);
    const std::string kind_key = normalize_token_cache_key(cache_kind);
    if (host_key.empty() || kind_key.empty()) {
        LOG_ERROR("TokenCache", "DELETE FAILED (empty key after normalization)");
        return false;
    }

    const char* sql = "DELETE FROM \"token-cache\" WHERE hostname = ? AND cache_kind = ?";
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("TokenCache", "DELETE ERROR preparing statement: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, host_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, kind_key.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (ok) {
        const int changes = sqlite3_changes(m_db);
        LOG_INFO("TokenCache", "DELETE OK host=" << host_key << " kind=" << kind_key
                  << " rows_removed=" << changes);
    } else {
        LOG_ERROR("TokenCache", "DELETE FAILED host=" << host_key << " kind=" << kind_key
                  << ": " << sqlite3_errmsg(m_db));
    }
    return ok;
}

// ============================================================================
// Feed Account Management
// ============================================================================

bool ConfigManager::add_feed_account(const FeedAccount& account) {
    if (!m_db || account.id.empty()) {
        return false;
    }

    const char* sql =
        "INSERT INTO feed_accounts(id, display_name, email, refresh_token, last_synced) "
        "VALUES(?, ?, ?, ?, ?)";
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare feed_accounts INSERT: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, account.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, account.refresh_token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, account.last_synced);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        LOG_ERROR("ConfigMgr", "Failed to add feed account: "
                  << sqlite3_errmsg(m_db));
    }
    return ok;
}

bool ConfigManager::update_feed_account(const FeedAccount& account) {
    if (!m_db || account.id.empty()) {
        return false;
    }

    const char* sql =
        "UPDATE feed_accounts SET display_name=?, email=?, refresh_token=?, last_synced=? "
        "WHERE id=?";
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare feed_accounts UPDATE: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, account.display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.refresh_token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, account.last_synced);
    sqlite3_bind_text(stmt, 5, account.id.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        LOG_ERROR("ConfigMgr", "Failed to update feed account: "
                  << sqlite3_errmsg(m_db));
    }
    return ok;
}

bool ConfigManager::delete_feed_account(std::string_view id) {
    if (!m_db || id.empty()) {
        return false;
    }

    const char* sql = "DELETE FROM feed_accounts WHERE id = ?";
    SqliteStmt stmt(m_db, sql);
    if (!stmt) {
        LOG_ERROR("ConfigMgr", "Failed to prepare feed_accounts DELETE: "
                  << sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, std::string{id}.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    return ok;
}

bool ConfigManager::clear_tokens_for_account(std::string_view account_id) {
    if (!m_db || account_id.empty()) {
        return false;
    }

    LOG_INFO("TokenCache", "CLEAR_FOR_ACCOUNT id=" << account_id);

    // Find all connections belonging to this account and delete their cached tokens
    int cleared = 0;
    for (const auto& conn : m_connections) {
        if (conn.source_account_id != account_id) continue;

        if (!conn.hostname.empty()) {
            delete_cached_token(conn.hostname, "machine");
            cleared++;
        }
        if (!conn.gateway_hostname.empty()) {
            delete_cached_token(conn.gateway_hostname, "gateway");
            cleared++;
        }
    }

    // Clear the account's refresh_token
    const char* sql = "UPDATE feed_accounts SET refresh_token = '' WHERE id = ?";
    SqliteStmt stmt(m_db, sql);
    if (stmt) {
        sqlite3_bind_text(stmt, 1, std::string{account_id}.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }

    LOG_INFO("TokenCache", "CLEAR_FOR_ACCOUNT id=" << account_id
              << " token_entries_cleared=" << cleared);
    return true;
}

bool ConfigManager::forget_account(std::string_view account_id) {
    if (!m_db || account_id.empty()) {
        return false;
    }

    LOG_INFO("ConfigMgr", "FORGET_ACCOUNT id=" << account_id);

    // Step 1: Clear cached tokens for this account's connections
    clear_tokens_for_account(account_id);

    // Step 2: Delete all connections imported by this account
    std::vector<std::string> to_delete;
    for (const auto& conn : m_connections) {
        if (conn.source_account_id == account_id) {
            to_delete.push_back(conn.name);
        }
    }
    for (const auto& name : to_delete) {
        (void)delete_connection(name);
    }
    LOG_INFO("ConfigMgr", "FORGET_ACCOUNT deleted " << to_delete.size() << " connections");

    // Step 3: Delete the feed account record
    (void)delete_feed_account(account_id);

    LOG_INFO("ConfigMgr", "FORGET_ACCOUNT complete id=" << account_id);
    return true;
}

std::vector<FeedAccount> ConfigManager::get_feed_accounts() const {
    std::vector<FeedAccount> accounts;
    if (!m_db) {
        return accounts;
    }

    const char* sql2 = "SELECT id, display_name, email, refresh_token, last_synced "
                      "FROM feed_accounts ORDER BY display_name COLLATE NOCASE";
    SqliteStmt stmt2(m_db, sql2);
    if (!stmt2) {
        LOG_ERROR("ConfigMgr", "Failed to prepare feed_accounts SELECT: "
                  << sqlite3_errmsg(m_db));
        return accounts;
    }

    while (sqlite3_step(stmt2) == SQLITE_ROW) {
        FeedAccount a;
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 0));
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 1));
        const auto* email = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 2));
        const auto* rt = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 3));
        a.id = id ? id : "";
        a.display_name = name ? name : "";
        a.email = email ? email : "";
        a.refresh_token = rt ? rt : "";
        a.last_synced = sqlite3_column_int64(stmt2, 4);
        accounts.push_back(std::move(a));
    }

    return accounts;
}

std::string ConfigManager::get_feed_accounts_json() const {
    const auto accounts = get_feed_accounts();
    json_utils::JsonPtr root{json_array()};
    for (const auto& a : accounts) {
        json_t* obj = json_object();
        json_object_set_new(obj, "id", json_string(a.id.c_str()));
        json_object_set_new(obj, "display_name", json_string(a.display_name.c_str()));
        json_object_set_new(obj, "email", json_string(a.email.c_str()));
        json_object_set_new(obj, "last_synced", json_integer(a.last_synced));
        json_array_append_new(root.get(), obj);
    }

    json_utils::MallocPtr dump{json_dumps(root.get(), JSON_COMPACT)};
    return std::string(dump ? dump.get() : "[]");
}
