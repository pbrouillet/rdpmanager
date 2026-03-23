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
            Some(n) => n,
            None => {
                error!("save_connection: missing 'name' field");
                return false;
            }
        };
        match db.execute(
            "INSERT OR REPLACE INTO connections (name, profile_json) VALUES (?1, ?2)",
            rusqlite::params![name, json_str],
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
        let tx = match db.execute("BEGIN", []) {
            Ok(_) => true,
            Err(_) => false,
        };
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

    // -- Internal helpers --

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
        "#;

        if let Err(e) = db.execute_batch(sql) {
            error!("Failed to ensure schema: {e}");
            return false;
        }
        true
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
