# SQLite Persistence in rdpmanager

## 1. Overview

rdpmanager uses **rusqlite 0.33** with the `bundled` and `backup` features (from `Cargo.toml`):

```toml
rusqlite = { version = "0.33", features = ["bundled", "backup"] }
```

- **`bundled`** — compiles SQLite from source into the binary; no system library required.
- **`backup`** — enables the SQLite backup API, used by `clone_database()`.

### Default paths

| Item | Location |
|------|----------|
| Database file | `~/.config/webui-rdp-client/connections.db` |
| App settings | `~/.config/webui-rdp-client/settings.json` |

The config directory is resolved via `dirs::config_dir()` with a fallback to
`.webui-rdp-client/` in the user home directory.

### Multi-database support

The application can open, create, clone, and switch databases at runtime. The
last-used database path is persisted in `settings.json` so the same DB reopens
on next launch. See [§6 Multi-Database Support](#6-multi-database-support) for
details.

---

## 2. Schema

`ensure_schema()` (in `src/config_manager.rs`) runs on every database open and
creates five tables:

```sql
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
```

### Table purposes

| Table | Purpose |
|-------|---------|
| `connections` | RDP connection profiles, stored as JSON blobs keyed by name. |
| `folders` | Hierarchical folder paths that organize connections (e.g. `"Prod/US-East"`). |
| `"token-cache"` | Cached AAD/OAuth tokens per hostname+kind. Quoted because of the hyphen. |
| `feed_accounts` | Azure Virtual Desktop feed accounts with refresh tokens. |
| `folder_settings` | Sparse JSON settings per folder, used for parameter inheritance. |

---

## 3. ConfigManager (`src/config_manager.rs`)

### Struct

```rust
#[derive(Debug, Default, Serialize, Deserialize)]
struct AppSettings {
    #[serde(default)]
    last_database_path: String,
}

pub struct ConfigManager {
    db: Option<Connection>,       // rusqlite::Connection
    database_path: PathBuf,
    config_dir: PathBuf,
    settings: AppSettings,
}
```

`db` is `Option<Connection>` — `None` when no database is open.

### Database Management

| Method | Description |
|--------|-------------|
| `new() -> Self` | Resolves config dir, loads `settings.json`, auto-opens the last-used DB (or falls back to `connections.db`). |
| `create_database(&mut self, path: &str) -> bool` | Creates a new DB file, calls `ensure_schema()`, switches to it, persists path in settings. |
| `open_database(&mut self, path: &str) -> bool` | Opens an existing DB, calls `ensure_schema()`, updates settings. |
| `clone_database(&mut self, source: &str, target: &str) -> bool` | Copies a DB using the SQLite backup API, then switches to the clone. See [§6](#6-multi-database-support). |
| `close_database(&mut self) -> bool` | Drops the `Connection`, sets `db` to `None`. |
| `has_open_database(&self) -> bool` | Returns `self.db.is_some()`. |
| `get_database_path(&self) -> String` | Returns the current database file path. |

**Startup flow in `new()`:**

```rust
pub fn new() -> Self {
    let config_dir = Self::resolve_config_dir();
    let mut mgr = Self { db: None, database_path: PathBuf::new(), config_dir, .. };
    mgr.load_settings();

    let startup_path = if mgr.settings.last_database_path.is_empty() {
        mgr.config_dir.join("connections.db")   // default
    } else {
        PathBuf::from(&mgr.settings.last_database_path)
    };

    if mgr.open_database_internal(&startup_path) {
        mgr.settings.last_database_path = startup_path.to_string_lossy().into_owned();
        mgr.save_settings();
    }
    mgr
}
```

### Connection CRUD

| Method | SQL | Returns |
|--------|-----|---------|
| `get_connections_json(&self)` | `SELECT name, profile_json FROM connections` | JSON array string (`"[]"` on error) |
| `get_connection_json(&self, name: &str)` | `SELECT profile_json FROM connections WHERE name = ?` | `Option<String>` — `None` if not found |
| `save_connection(&self, json_str: &str)` | Parse JSON → `INSERT OR REPLACE INTO connections (name, profile_json) VALUES (?, ?)` | `bool` |
| `delete_connection(&self, name: &str)` | `DELETE FROM connections WHERE name = ?` | `bool` |

`save_connection` parses the incoming JSON to extract the `name` field, then
stores the entire blob as `profile_json`.

### Folder CRUD

| Method | Description |
|--------|-------------|
| `get_folders_json(&self)` | `SELECT path FROM folders ORDER BY path` → JSON array. |
| `create_folder(&self, path: &str)` | `INSERT OR IGNORE INTO folders (path) VALUES (?)`. |
| `move_folder(&self, source: &str, target_parent: &str)` | Renames the folder and all child folders (path prefix update), then updates `folder` field in matching connection profiles. |
| `rename_folder(&self, source: &str, new_name: &str)` | Convenience wrapper — computes the new path and delegates to `move_folder`. |
| `delete_folder(&self, path: &str)` | Deletes the folder, all child folders (prefix match), and their `folder_settings` entries. |

**`move_folder` implementation detail:** The method updates folder paths and
connection profile JSON in a single transaction. For each affected connection it
deserializes `profile_json`, patches the `folder` field, and writes it back.

### Folder Settings

| Method | Description |
|--------|-------------|
| `get_folder_settings(&self, path: &str) -> String` | Returns `settings_json` for the given path, or `"{}"` if not found. |
| `save_folder_settings(&self, path: &str, settings_json: &str) -> bool` | `INSERT OR REPLACE INTO folder_settings`. |

---

## 4. Connection Profiles as JSON Blobs

Connection profiles are defined as `ConnectionProfile` in `src/types.rs` and
serialized to/from JSON via serde:

```rust
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ConnectionProfile {
    pub name: String,
    pub folder: String,
    pub hostname: String,
    #[serde(default = "default_port")]
    pub port: u16,                          // 3389
    pub username: String,
    pub domain: String,

    // Display
    #[serde(default = "default_width")]
    pub width: u32,                         // 1920
    #[serde(default = "default_height")]
    pub height: u32,                        // 1080
    pub fullscreen: bool,

    // RDP features (booleans)
    pub home_drive: bool,
    pub clipboard: bool,
    pub cert_tofu: bool,
    pub usb_auto: bool,
    pub floatbar: bool,
    pub dynamic_resolution: bool,
    pub network_auto: bool,
    pub gfx_avc420: bool,
    pub compression: bool,
    pub audio_pulse: bool,
    pub prevent_session_lock: bool,
    #[serde(default = "default_true")]
    pub auto_reconnect: bool,              // true
    #[serde(default = "default_retries")]
    pub auto_reconnect_max_retries: i32,   // 3

    // Gateway & AAD/AVD
    pub gateway_hostname: String,
    pub enable_rds_aad_auth: bool,
    pub target_is_aad_joined: bool,
    pub load_balance_info: String,
    pub use_manual_code_flow: bool,
    pub aad_tenant_id: String,
    pub wvd_endpoint_pool: String,
    pub workspace_id: String,
    pub arm_path: String,
    pub remote_application_program: String,
    pub remote_desktop_name: String,
    pub source_account_id: String,

    /// Fields explicitly set on this connection (vs inherited from folder).
    #[serde(default)]
    pub overridden_fields: Vec<String>,
}
```

The entire struct is stored as a single `profile_json` TEXT column in the
`connections` table. This keeps the schema stable — new fields are added to the
Rust struct and serialized automatically without schema migrations.

### `overridden_fields`

A list of field names that were **explicitly set** on this connection rather
than inherited from the containing folder. The UI uses this to distinguish
between a user-chosen value and one that came from folder defaults. Fields
*not* in this list should defer to the folder's settings.

---

## 5. Folder Settings Inheritance

### FolderSettings type (`src/types.rs`)

All fields are `Option<T>` — only explicitly-set values are serialized:

```rust
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct FolderSettings {
    pub home_drive: Option<bool>,
    pub clipboard: Option<bool>,
    pub cert_tofu: Option<bool>,
    pub usb_auto: Option<bool>,
    pub floatbar: Option<bool>,
    pub dynamic_resolution: Option<bool>,
    pub network_auto: Option<bool>,
    pub gfx_avc420: Option<bool>,
    pub compression: Option<bool>,
    pub audio_pulse: Option<bool>,
    pub prevent_session_lock: Option<bool>,
    pub auto_reconnect: Option<bool>,
    pub auto_reconnect_max_retries: Option<i32>,
    pub gateway_hostname: Option<String>,
    pub enable_rds_aad_auth: Option<bool>,
    pub target_is_aad_joined: Option<bool>,
    pub load_balance_info: Option<String>,
    pub use_manual_code_flow: Option<bool>,
}
```

### Design

Folder settings use a **sparse override** pattern:

1. Each folder has an optional `folder_settings` row with only the fields that
   differ from the default.
2. Connections belong to a folder (`ConnectionProfile.folder`).
3. When resolving effective settings for a connection, the intended flow is:
   - Start with application defaults.
   - Walk the folder hierarchy from root to the connection's folder, merging
     each ancestor's `FolderSettings` (set fields override parent values).
   - Apply the connection's own values for fields listed in `overridden_fields`.

### Current status

The JS bindings expose two related endpoints in `src/js_handlers.rs`:

- **`on_get_effective_folder_settings`** — currently returns direct folder
  settings (inheritance walk **not yet implemented**).
- **`on_get_effective_connection_profile`** — currently returns the raw stored
  profile without folder merging.

> **TODO:** Implement the ancestor-walk merge so that a connection under
> `Prod/US-East` inherits settings from `Prod/` and then `Prod/US-East/`,
> with `overridden_fields` taking final precedence.

---

## 6. Multi-Database Support

### Application settings (`settings.json`)

Application-level settings live **outside** SQLite in a JSON file:

```json
{
  "last_database_path": "/home/user/.config/webui-rdp-client/connections.db"
}
```

This is managed by `AppSettings` / `load_settings()` / `save_settings()` in
`config_manager.rs`. Every call to `create_database`, `open_database`, or
`clone_database` updates `last_database_path` and writes the file.

### Database operations

| Operation | What happens |
|-----------|-------------|
| **Create** | `Connection::open(path)` + `ensure_schema()` → empty DB with all 5 tables. |
| **Open** | `Connection::open(path)` + `ensure_schema()` → adds any missing tables. |
| **Clone** | Uses the SQLite backup API (see below) for an atomic, consistent copy. |
| **Switch** | Close current `Connection`, open new one, update settings. |

### Clone via SQLite Backup API

`clone_database` uses `rusqlite::backup::Backup` for a reliable, page-level copy:

```rust
pub fn clone_database(&mut self, source: &str, target: &str) -> bool {
    // Open source read-only
    let src_conn = Connection::open_with_flags(
        source,
        rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY,
    )?;

    let mut dst_conn = Connection::open(target)?;

    // Create backup handle
    let backup = rusqlite::backup::Backup::new_with_names(
        &src_conn,
        rusqlite::DatabaseName::Main,
        &mut dst_conn,
        rusqlite::DatabaseName::Main,
    )?;

    // Copy entire database in one step (-1 = all pages)
    backup.step(-1)?;

    // Switch to cloned database
    self.open_database_internal(&PathBuf::from(target));
    self.settings.last_database_path = target.to_string();
    self.save_settings()
}
```

Using the backup API instead of file-copy ensures the target DB is in a
consistent state even if the source is being written to concurrently.

---

## 7. Thread Safety

### Global state (`src/js_handlers.rs`)

`ConfigManager` is stored in a process-global `OnceLock<Arc<Mutex<>>>`:

```rust
static STATE: OnceLock<Arc<Mutex<ConfigManager>>> = OnceLock::new();
```

### `with_config()` helper

All JS→Rust bindings access the manager through a single closure-based helper
that locks the mutex:

```rust
fn with_config<F, R>(f: F) -> Option<R>
where
    F: FnOnce(&mut ConfigManager) -> R,
{
    STATE.get().and_then(|arc| {
        let mut guard = arc.lock().ok()?;
        Some(f(&mut guard))
    })
}
```

**Example — saving a connection:**

```rust
unsafe extern "C" fn on_save_connection(e: *mut webui_sys::webui_event_t) {
    let json_str = unsafe { get_string_at(e, 0) };
    let ok = with_config(|cm| cm.save_connection(&json_str)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}
```

### Concurrency model

- **Single writer** — the `Mutex` ensures only one thread accesses
  `ConfigManager` (and the underlying `rusqlite::Connection`) at a time.
- **No WAL mode** — SQLite is used in the default journal mode; the mutex makes
  WAL unnecessary.
- WebUI JS callbacks can fire from any thread. The `with_config()` pattern
  serializes all database access regardless of the calling thread.

---

## Cross-Reference: Key Files

| File | Role |
|------|------|
| `src/config_manager.rs` | All database logic — CRUD, schema, backup, settings persistence. |
| `src/types.rs` | `ConnectionProfile` and `FolderSettings` struct definitions. |
| `src/js_handlers.rs` | JS↔Rust bindings; `Arc<Mutex<ConfigManager>>` wrapper and `with_config()`. |
| `Cargo.toml` | `rusqlite` dependency with `bundled` + `backup` features. |
| `~/.config/webui-rdp-client/settings.json` | Persists `last_database_path` across sessions. |
