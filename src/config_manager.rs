//! SQLite persistence layer for connections, folders, tokens, and feed accounts.
//!
//! Manages the `connections.db` SQLite database with tables for:
//! - `connections` — RDP connection profiles (JSON blob per entry)
//! - `folders` — Hierarchical folder organization
//! - `folder_settings` — Sparse parameter inheritance settings
//! - `feed_accounts` — WVD/AVD account metadata
//! - `token_cache` — OAuth token caching with expiration
//!
//! Supports multiple databases (create, open, clone, switch).
//!
//! Equivalent to: `src/config_manager.cpp` / `config_manager.hpp`

use log::{error, info, warn};
use rusqlite::Connection;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

/// Persisted settings (last-used database path, etc.)
#[derive(Debug, Default, Serialize, Deserialize)]
struct AppSettings {
    #[serde(default)]
    last_database_path: String,
}

pub struct ConfigManager {
    db: Option<Connection>,
    database_path: PathBuf,
    config_dir: PathBuf,
    settings: AppSettings,
}

impl ConfigManager {
    /// Create a new ConfigManager and open the last-used (or default) database.
    pub fn new() -> Self {
        let config_dir = Self::resolve_config_dir();
        if !config_dir.exists() {
            let _ = fs::create_dir_all(&config_dir);
        }

        let mut mgr = Self {
            db: None,
            database_path: PathBuf::new(),
            config_dir: config_dir.clone(),
            settings: AppSettings::default(),
        };

        mgr.load_settings();

        let startup_path = if mgr.settings.last_database_path.is_empty() {
            config_dir.join("connections.db")
        } else {
            PathBuf::from(&mgr.settings.last_database_path)
        };

        if mgr.open_database_internal(&startup_path) {
            mgr.settings.last_database_path = startup_path.to_string_lossy().into_owned();
            mgr.save_settings();
            info!("Opened database: {}", startup_path.display());
        } else {
            warn!(
                "Failed to open startup database: {}",
                startup_path.display()
            );
        }

        mgr
    }

    // -- Public database operations --

    pub fn create_database(&mut self, path: &str) -> bool {
        if path.is_empty() {
            return false;
        }
        let p = PathBuf::from(path);
        if !self.open_database_internal(&p) {
            return false;
        }
        self.settings.last_database_path = path.to_string();
        self.save_settings()
    }

    pub fn open_database(&mut self, path: &str) -> bool {
        if path.is_empty() {
            return false;
        }
        let p = PathBuf::from(path);
        if !self.open_database_internal(&p) {
            return false;
        }
        self.settings.last_database_path = path.to_string();
        self.save_settings()
    }

    pub fn clone_database(&mut self, source: &str, target: &str) -> bool {
        if source.is_empty() || target.is_empty() || source == target {
            return false;
        }

        let target_path = PathBuf::from(target);
        if let Some(parent) = target_path.parent() {
            if !parent.exists() {
                if let Err(e) = fs::create_dir_all(parent) {
                    error!("Failed to create clone target dir: {e}");
                    return false;
                }
            }
        }

        // Use SQLite backup API via rusqlite
        let src_conn =
            match Connection::open_with_flags(source, rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY) {
                Ok(c) => c,
                Err(e) => {
                    error!("Failed to open source for clone: {e}");
                    return false;
                }
            };

        let mut dst_conn = match Connection::open(target) {
            Ok(c) => c,
            Err(e) => {
                error!("Failed to open target for clone: {e}");
                return false;
            }
        };

        let backup = match rusqlite::backup::Backup::new_with_names(
            &src_conn,
            rusqlite::DatabaseName::Main,
            &mut dst_conn,
            rusqlite::DatabaseName::Main,
        ) {
            Ok(b) => b,
            Err(e) => {
                error!("Failed to init backup: {e}");
                return false;
            }
        };

        if let Err(e) = backup.step(-1) {
            error!("Backup step failed: {e}");
            return false;
        }

        drop(backup);
        drop(dst_conn);
        drop(src_conn);

        if !self.open_database_internal(&target_path) {
            return false;
        }
        self.settings.last_database_path = target.to_string();
        self.save_settings()
    }

    pub fn close_database(&mut self) -> bool {
        self.db = None;
        self.database_path = PathBuf::new();
        self.settings.last_database_path.clear();
        self.save_settings()
    }

    pub fn has_open_database(&self) -> bool {
        self.db.is_some()
    }

    pub fn get_database_path(&self) -> String {
        self.database_path.to_string_lossy().into_owned()
    }

    // -- Connection CRUD --

    pub fn get_connections_json(&self) -> String {
        let Some(ref db) = self.db else {
            return "[]".to_string();
        };
        let mut stmt = match db.prepare("SELECT name, profile_json FROM connections") {
            Ok(s) => s,
            Err(_) => return "[]".to_string(),
        };
        let rows: Vec<String> = match stmt.query_map([], |row| row.get::<_, String>(1)) {
            Ok(iter) => iter.filter_map(|r| r.ok()).collect(),
            Err(_) => return "[]".to_string(),
        };
        format!("[{}]", rows.join(","))
    }

    pub fn get_connection_json(&self, name: &str) -> Option<String> {
        let db = self.db.as_ref()?;
        db.query_row(
            "SELECT profile_json FROM connections WHERE name = ?1",
            [name],
            |row| row.get::<_, String>(0),
        )
        .ok()
    }

    pub fn save_connection(&self, json_str: &str) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        // Extract the "name" field from the JSON to use as primary key
        let val: serde_json::Value = match serde_json::from_str(json_str) {
            Ok(v) => v,
            Err(e) => {
                error!("save_connection: invalid JSON: {e}");
                return false;
            }
        };
        let name = match val.get("name").and_then(|n| n.as_str()) {
            Some(n) => n.to_string(),
            None => {
                error!("save_connection: missing 'name' field");
                return false;
            }
        };

        // Handle password encryption: if plaintext_password is provided,
        // encrypt it into encrypted_password and strip the plaintext.
        let mut val = val;
        if let Some(plaintext) = val.get("plaintext_password").and_then(|v| v.as_str()) {
            if !plaintext.is_empty() {
                if let Some(encrypted) = self.encrypt_password(plaintext) {
                    val["encrypted_password"] = serde_json::Value::String(encrypted);
                } else {
                    error!("save_connection: failed to encrypt password");
                }
            }
        }
        // Never persist plaintext password or save_password flag
        if let Some(obj) = val.as_object_mut() {
            obj.remove("plaintext_password");
            obj.remove("save_password");
        }

        let sanitized_json = serde_json::to_string(&val).unwrap_or_default();
        match db.execute(
            "INSERT OR REPLACE INTO connections (name, profile_json) VALUES (?1, ?2)",
            rusqlite::params![name, sanitized_json],
        ) {
            Ok(_) => {
                info!("Saved connection: {name}");
                true
            }
            Err(e) => {
                error!("save_connection failed: {e}");
                false
            }
        }
    }

    pub fn delete_connection(&self, name: &str) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        match db.execute(
            "DELETE FROM connections WHERE name = ?1",
            rusqlite::params![name],
        ) {
            Ok(_) => {
                info!("Deleted connection: {name}");
                true
            }
            Err(e) => {
                error!("delete_connection failed: {e}");
                false
            }
        }
    }

    // -- Folder CRUD --

    pub fn get_folders_json(&self) -> String {
        let Some(ref db) = self.db else {
            return "[]".to_string();
        };
        let mut stmt = match db.prepare("SELECT path FROM folders ORDER BY path") {
            Ok(s) => s,
            Err(_) => return "[]".to_string(),
        };
        let paths: Vec<String> = match stmt.query_map([], |row| {
            let p: String = row.get(0)?;
            Ok(format!(
                "\"{}\"",
                p.replace('\\', "\\\\").replace('"', "\\\"")
            ))
        }) {
            Ok(iter) => iter.filter_map(|r| r.ok()).collect(),
            Err(_) => return "[]".to_string(),
        };
        format!("[{}]", paths.join(","))
    }

    pub fn create_folder(&self, path: &str) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        match db.execute(
            "INSERT OR IGNORE INTO folders (path) VALUES (?1)",
            rusqlite::params![path],
        ) {
            Ok(_) => true,
            Err(e) => {
                error!("create_folder failed: {e}");
                false
            }
        }
    }

    pub fn move_folder(&self, source: &str, target_parent: &str) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        // Extract the folder's leaf name and build the new path
        let leaf = source.rsplit('/').next().unwrap_or(source);
        let new_path = if target_parent.is_empty() {
            leaf.to_string()
        } else {
            format!("{target_parent}/{leaf}")
        };
        let tx = db.execute("BEGIN", []).is_ok();
        // Rename the folder itself
        let _ = db.execute(
            "UPDATE folders SET path = ?1 WHERE path = ?2",
            rusqlite::params![new_path, source],
        );
        // Rename all child folders (prefix match)
        let prefix = format!("{source}/");
        let _ = db.execute(
            "UPDATE folders SET path = ?1 || substr(path, ?2) WHERE path LIKE ?3",
            rusqlite::params![
                format!("{new_path}/"),
                prefix.len() + 1,
                format!("{prefix}%")
            ],
        );
        // Move connections in this folder
        let _ = db.execute(
            "UPDATE connections SET profile_json = json_set(profile_json, '$.folder', ?1) WHERE json_extract(profile_json, '$.folder') = ?2",
            rusqlite::params![new_path, source],
        );
        if tx {
            let _ = db.execute("COMMIT", []);
        }
        true
    }

    pub fn rename_folder(&self, source: &str, new_name: &str) -> bool {
        // Build new path by replacing the last segment
        let parent = source.rsplit_once('/').map(|(p, _)| p).unwrap_or("");
        let new_path = if parent.is_empty() {
            new_name.to_string()
        } else {
            format!("{parent}/{new_name}")
        };
        self.move_folder(source, parent)
            && self
                .db
                .as_ref()
                .map(|db| {
                    // Fix the exact folder name (move_folder rebuilds from leaf)
                    let _ = db.execute(
                        "UPDATE folders SET path = ?1 WHERE path LIKE ?2",
                        rusqlite::params![
                            new_path,
                            format!(
                                "{}/{}",
                                if parent.is_empty() {
                                    "".to_string()
                                } else {
                                    parent.to_string()
                                },
                                source.rsplit('/').next().unwrap_or(source)
                            )
                        ],
                    );
                    true
                })
                .unwrap_or(false)
    }

    pub fn delete_folder(&self, path: &str) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        let _ = db.execute(
            "DELETE FROM folders WHERE path = ?1 OR path LIKE ?2",
            rusqlite::params![path, format!("{path}/%")],
        );
        let _ = db.execute(
            "DELETE FROM folder_settings WHERE path = ?1 OR path LIKE ?2",
            rusqlite::params![path, format!("{path}/%")],
        );
        true
    }

    // -- Folder settings --

    pub fn get_folder_settings(&self, path: &str) -> String {
        let Some(ref db) = self.db else {
            return "{}".to_string();
        };
        db.query_row(
            "SELECT settings_json FROM folder_settings WHERE path = ?1",
            rusqlite::params![path],
            |row| row.get::<_, String>(0),
        )
        .unwrap_or_else(|_| "{}".to_string())
    }

    pub fn save_folder_settings(&self, path: &str, settings_json: &str) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        match db.execute(
            "INSERT OR REPLACE INTO folder_settings (path, settings_json) VALUES (?1, ?2)",
            rusqlite::params![path, settings_json],
        ) {
            Ok(_) => true,
            Err(e) => {
                error!("save_folder_settings failed: {e}");
                false
            }
        }
    }

    // -- Token cache CRUD --

    /// Look up a cached token. Returns `None` if not found or expired.
    pub fn get_cached_token(&self, key: &str, scope: &str) -> Option<String> {
        let db = self.db.as_ref()?;
        let now = epoch_secs();
        db.query_row(
            "SELECT token FROM token_cache WHERE cache_key = ?1 AND scope = ?2 AND expires_at > ?3",
            rusqlite::params![key, scope, now],
            |row| row.get::<_, String>(0),
        )
        .ok()
    }

    /// Store a token in the cache with expiration.
    pub fn set_cached_token(&self, key: &str, scope: &str, token: &str, expires_at: i64) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        match db.execute(
            "INSERT OR REPLACE INTO token_cache (cache_key, scope, token, expires_at) VALUES (?1, ?2, ?3, ?4)",
            rusqlite::params![key, scope, token, expires_at],
        ) {
            Ok(_) => {
                info!("Cached token for key={key} scope={scope}");
                true
            }
            Err(e) => {
                error!("set_cached_token failed: {e}");
                false
            }
        }
    }

    /// Delete a specific cached token.
    pub fn delete_cached_token(&self, key: &str, scope: &str) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        match db.execute(
            "DELETE FROM token_cache WHERE cache_key = ?1 AND scope = ?2",
            rusqlite::params![key, scope],
        ) {
            Ok(_) => true,
            Err(e) => {
                error!("delete_cached_token failed: {e}");
                false
            }
        }
    }

    /// Get all feed accounts as a JSON array string.
    pub fn get_feed_accounts_json(&self) -> String {
        let Some(ref db) = self.db else {
            return "[]".to_string();
        };
        let mut stmt = match db.prepare(
            "SELECT id, display_name, email, last_synced FROM feed_accounts ORDER BY display_name",
        ) {
            Ok(s) => s,
            Err(_) => return "[]".to_string(),
        };
        let rows: Vec<String> = match stmt.query_map([], |row| {
            let id: String = row.get(0)?;
            let display_name: String = row.get(1)?;
            let email: String = row.get(2)?;
            let last_synced: i64 = row.get(3)?;
            Ok(serde_json::json!({
                "id": id,
                "display_name": display_name,
                "email": email,
                "last_synced": last_synced,
            })
            .to_string())
        }) {
            Ok(iter) => iter.filter_map(|r| r.ok()).collect(),
            Err(_) => return "[]".to_string(),
        };
        format!("[{}]", rows.join(","))
    }

    /// Insert a new feed account record.
    pub fn add_feed_account(
        &self,
        id: &str,
        display_name: &str,
        email: &str,
        last_synced: i64,
    ) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        match db.execute(
            "INSERT OR REPLACE INTO feed_accounts (id, display_name, email, last_synced) VALUES (?1, ?2, ?3, ?4)",
            rusqlite::params![id, display_name, email, last_synced],
        ) {
            Ok(_) => {
                info!("Added feed account: {display_name} ({id})");
                true
            }
            Err(e) => {
                error!("add_feed_account failed: {e}");
                false
            }
        }
    }

    /// Update an existing feed account record.
    pub fn update_feed_account(
        &self,
        id: &str,
        display_name: &str,
        email: &str,
        last_synced: i64,
    ) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        match db.execute(
            "UPDATE feed_accounts SET display_name = ?2, email = ?3, last_synced = ?4 WHERE id = ?1",
            rusqlite::params![id, display_name, email, last_synced],
        ) {
            Ok(n) => {
                if n == 0 {
                    // Row didn't exist — insert instead
                    return self.add_feed_account(id, display_name, email, last_synced);
                }
                info!("Updated feed account: {display_name} ({id})");
                true
            }
            Err(e) => {
                error!("update_feed_account failed: {e}");
                false
            }
        }
    }

    /// Delete a feed account record (without clearing tokens).
    pub fn delete_feed_account(&self, account_id: &str) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        match db.execute(
            "DELETE FROM feed_accounts WHERE id = ?1",
            rusqlite::params![account_id],
        ) {
            Ok(n) => n > 0,
            Err(e) => {
                error!("delete_feed_account failed: {e}");
                false
            }
        }
    }

    /// Delete a feed account and all its cached tokens.
    pub fn forget_account(&self, account_id: &str) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        let deleted = match db.execute(
            "DELETE FROM feed_accounts WHERE id = ?1",
            rusqlite::params![account_id],
        ) {
            Ok(n) => n > 0,
            Err(e) => {
                error!("forget_account: DELETE failed: {e}");
                return false;
            }
        };
        self.clear_tokens_for_account(account_id);
        deleted
    }

    /// Clear all tokens for a given key prefix (used when logging off an account).
    pub fn clear_tokens_for_account(&self, key_prefix: &str) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };
        let pattern = format!("{key_prefix}%");
        match db.execute(
            "DELETE FROM token_cache WHERE cache_key LIKE ?1",
            rusqlite::params![pattern],
        ) {
            Ok(n) => {
                info!("Cleared {n} cached tokens for prefix={key_prefix}");
                true
            }
            Err(e) => {
                error!("clear_tokens_for_account failed: {e}");
                false
            }
        }
    }

    // -- Folder settings inheritance --

    /// Get effective (cascade-resolved) folder settings as a JSON string.
    /// Walks the ancestor chain from root to the given path, merging at each level.
    pub fn get_effective_folder_settings(&self, path: &str) -> String {
        let Some(ref db) = self.db else {
            return "{}".to_string();
        };

        let ancestors = Self::get_ancestor_paths(path);
        let mut merged = serde_json::Value::Object(serde_json::Map::new());

        // Walk root → leaf so leaf values override parent values
        for ancestor in &ancestors {
            let json_str: Option<String> = db
                .query_row(
                    "SELECT settings_json FROM folder_settings WHERE path = ?1",
                    rusqlite::params![ancestor],
                    |row| row.get::<_, String>(0),
                )
                .ok();

            if let Some(s) = json_str {
                if let Ok(overlay) = serde_json::from_str::<serde_json::Value>(&s) {
                    merge_json(&mut merged, &overlay);
                }
            }
        }

        serde_json::to_string(&merged).unwrap_or_else(|_| "{}".to_string())
    }

    /// Get a fully resolved connection profile with folder settings inheritance applied.
    /// Folder defaults are applied only for fields NOT in the connection's `overridden_fields`.
    pub fn get_effective_connection_profile(&self, name: &str) -> Option<String> {
        let db = self.db.as_ref()?;

        let profile_json: String = db
            .query_row(
                "SELECT profile_json FROM connections WHERE name = ?1",
                [name],
                |row| row.get::<_, String>(0),
            )
            .ok()?;

        let mut profile: serde_json::Value = serde_json::from_str(&profile_json).ok()?;

        // Determine the folder path from the connection profile
        let folder = profile
            .get("folder")
            .and_then(|f| f.as_str())
            .unwrap_or("")
            .to_string();

        // Get effective folder settings for this folder
        let effective_settings_str = self.get_effective_folder_settings(&folder);
        let folder_settings: serde_json::Value =
            serde_json::from_str(&effective_settings_str).unwrap_or(serde_json::Value::Null);

        // Determine which fields are explicitly overridden on this connection
        let overridden: std::collections::HashSet<String> = profile
            .get("overridden_fields")
            .and_then(|v| v.as_array())
            .map(|arr| {
                arr.iter()
                    .filter_map(|v| v.as_str().map(|s| s.to_string()))
                    .collect()
            })
            .unwrap_or_default();

        let legacy = !profile
            .as_object()
            .map(|o| o.contains_key("overridden_fields"))
            .unwrap_or(false);

        // Apply folder defaults for fields NOT overridden by the connection.
        // Legacy profiles (no overridden_fields key) keep all their values.
        if !legacy {
            if let Some(settings_obj) = folder_settings.as_object() {
                if let Some(profile_obj) = profile.as_object_mut() {
                    for (key, value) in settings_obj {
                        if !overridden.contains(key) {
                            profile_obj.insert(key.clone(), value.clone());
                        }
                    }
                }
            }
        }

        serde_json::to_string(&profile).ok()
    }

    // -- Internal helpers --

    /// Given a folder path like "a/b/c", returns `["", "a", "a/b", "a/b/c"]`.
    /// The empty string represents root-level (global) defaults.
    fn get_ancestor_paths(path: &str) -> Vec<String> {
        let mut ancestors = vec![String::new()]; // root
        if path.is_empty() {
            return ancestors;
        }

        let mut current = String::new();
        for (i, segment) in path.split('/').enumerate() {
            if i == 0 {
                current = segment.to_string();
            } else {
                current = format!("{current}/{segment}");
            }
            ancestors.push(current.clone());
        }
        ancestors
    }

    fn open_database_internal(&mut self, path: &Path) -> bool {
        if let Some(parent) = path.parent() {
            if !parent.as_os_str().is_empty() && !parent.exists() {
                if let Err(e) = fs::create_dir_all(parent) {
                    error!("Failed to create database dir: {e}");
                    return false;
                }
            }
        }

        let conn = match Connection::open(path) {
            Ok(c) => c,
            Err(e) => {
                error!("sqlite open failed for {}: {e}", path.display());
                return false;
            }
        };

        // Close existing connection
        self.db = None;
        self.db = Some(conn);
        self.database_path = path.to_path_buf();

        if !self.ensure_schema() {
            self.db = None;
            self.database_path = PathBuf::new();
            return false;
        }

        true
    }

    fn ensure_schema(&self) -> bool {
        let Some(ref db) = self.db else {
            return false;
        };

        let sql = r#"
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
            );
            CREATE TABLE IF NOT EXISTS token_cache (
                cache_key TEXT NOT NULL,
                scope TEXT NOT NULL,
                token TEXT NOT NULL,
                expires_at INTEGER NOT NULL,
                PRIMARY KEY (cache_key, scope)
            );
            CREATE TABLE IF NOT EXISTS settings (
                key TEXT PRIMARY KEY NOT NULL,
                value TEXT NOT NULL
            );
        "#;

        if let Err(e) = db.execute_batch(sql) {
            error!("Failed to ensure schema: {e}");
            return false;
        }
        true
    }

    /// Get or create the database encryption key for password storage.
    /// The key is stored in the settings table as a hex-encoded 256-bit value.
    pub fn get_encryption_key(&self) -> Option<[u8; 32]> {
        let db = self.db.as_ref()?;

        // Try to load existing key
        let existing: Option<String> = db
            .query_row(
                "SELECT value FROM settings WHERE key = 'encryption_key'",
                [],
                |row| row.get(0),
            )
            .ok();

        if let Some(hex) = existing {
            match crate::crypto::key_from_hex(&hex) {
                Ok(key) => return Some(key),
                Err(e) => {
                    error!("Corrupt encryption key in settings: {e}");
                    return None;
                }
            }
        }

        // Generate and store a new key
        let key = crate::crypto::generate_key();
        let hex = crate::crypto::key_to_hex(&key);
        match db.execute(
            "INSERT INTO settings (key, value) VALUES ('encryption_key', ?1)",
            rusqlite::params![hex],
        ) {
            Ok(_) => {
                info!("Generated new database encryption key");
                Some(key)
            }
            Err(e) => {
                error!("Failed to store encryption key: {e}");
                None
            }
        }
    }

    /// Encrypt a plaintext password using the database encryption key.
    pub fn encrypt_password(&self, plaintext: &str) -> Option<String> {
        let key = self.get_encryption_key()?;
        match crate::crypto::encrypt(&key, plaintext) {
            Ok(encrypted) => Some(encrypted),
            Err(e) => {
                error!("Password encryption failed: {e}");
                None
            }
        }
    }

    /// Decrypt an encrypted password using the database encryption key.
    pub fn decrypt_password(&self, encrypted: &str) -> Option<String> {
        let key = self.get_encryption_key()?;
        match crate::crypto::decrypt(&key, encrypted) {
            Ok(plaintext) => Some(plaintext),
            Err(e) => {
                error!("Password decryption failed: {e}");
                None
            }
        }
    }

    fn resolve_config_dir() -> PathBuf {
        dirs::config_dir()
            .map(|d| d.join("webui-rdp-client"))
            .unwrap_or_else(|| PathBuf::from(".webui-rdp-client"))
    }

    fn settings_path(&self) -> PathBuf {
        self.config_dir.join("settings.json")
    }

    fn load_settings(&mut self) {
        let path = self.settings_path();
        if let Ok(data) = fs::read_to_string(&path) {
            if let Ok(s) = serde_json::from_str::<AppSettings>(&data) {
                self.settings = s;
            }
        }
    }

    fn save_settings(&self) -> bool {
        let path = self.settings_path();
        match serde_json::to_string_pretty(&self.settings) {
            Ok(json) => {
                if let Err(e) = fs::write(&path, json) {
                    error!("Failed to save settings: {e}");
                    return false;
                }
                true
            }
            Err(e) => {
                error!("Failed to serialize settings: {e}");
                false
            }
        }
    }
}

/// Merge `overlay` JSON object fields into `base`. Overlay values win.
fn merge_json(base: &mut serde_json::Value, overlay: &serde_json::Value) {
    if let (Some(base_obj), Some(overlay_obj)) = (base.as_object_mut(), overlay.as_object()) {
        for (key, value) in overlay_obj {
            base_obj.insert(key.clone(), value.clone());
        }
    }
}

/// Current epoch time in seconds.
fn epoch_secs() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}
