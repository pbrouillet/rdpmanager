# rdpmanager — Architecture Documentation

## 1. Overview

**rdpmanager** is a cross-platform RDP (Remote Desktop Protocol) client built in Rust. It combines a modern React 19 / Fluent UI frontend with a Rust backend, using FreeRDP3 for the RDP protocol and WebUI for the browser-based UI layer.

### Migration Context

The project was originally a C++23 application built with Meson + Ninja. It has been ported to Rust with Cargo as the build system, preserving the same architecture while leveraging Rust's ownership model, type safety, and memory safety guarantees. The C++ source files remain in the tree for reference but are no longer compiled. See [MODERNIZATION.md](../MODERNIZATION.md) for details on C++ modernization passes that informed the Rust port.

### Key Design Decisions

- **Single-binary deployment**: React assets are embedded at compile time via a generated VFS module, producing a self-contained executable.
- **FreeRDP built from source**: The FreeRDP3 library is compiled as a static library via CMake during `cargo build`, with two critical patches applied automatically.
- **WebUI as the UI layer**: Rather than GTK, Qt, or a custom toolkit, rdpmanager uses [WebUI](https://webui.me/) to host a full React application inside a native WebView (WebKitGTK on Linux, WebView2 on Windows, WKWebView on macOS).
- **SQLite for persistence**: All connection profiles, folders, folder settings, tokens, and feed accounts are stored in a SQLite database with support for multiple database files.

---

## 2. High-Level Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         React / Fluent UI                                │
│                        (src/ui-react/)                                   │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────┐             │
│   │Connection │  │  Folder  │  │ Session  │  │   Feed     │             │
│   │  Editor   │  │  Tree    │  │  Tabs    │  │ Discovery  │             │
│   └────┬─────┘  └────┬─────┘  └────┬─────┘  └─────┬──────┘             │
│        │              │             │               │                    │
│        └──────────┬───┘─────────────┘───────────────┘                    │
│                   │  JS function calls (e.g. connectRDP(), saveConn())   │
└───────────────────┼──────────────────────────────────────────────────────┘
                    │  WebUI JS ↔ Rust binding layer
┌───────────────────┼──────────────────────────────────────────────────────┐
│                   ▼                                                      │
│          js_handlers.rs  (33 registered bindings)                        │
│                   │                                                      │
│       ┌───────────┼────────────────┐                                     │
│       ▼           ▼                ▼                                     │
│  ConfigManager  SessionManager   utils.rs                                │
│  (SQLite CRUD)  (tab coord.)    (JWT, URL, PKCE)                        │
│       │           │                                                      │
│       │     ┌─────┴──────┐                                               │
│       │     ▼            ▼                                               │
│       │  RDPLauncher  EmbeddingHost                                      │
│       │  (FreeRDP)    (window reparenting)                               │
│       │     │                                                            │
│       │     ▼                                                            │
│       │  freerdp-sys (FFI → libfreerdp3.a)                               │
│       │                                                                  │
│       └──→ rusqlite (bundled SQLite)                                     │
│                                                                          │
│                    Rust Backend                                           │
└──────────────────────────────────────────────────────────────────────────┘
```

### Compact Dependency Tree

```
main.rs
  └─→ MainWindow::new() / initialize() / show() / wait()
       ├─→ js_handlers::bind_all()    — register 33 JS↔Rust bindings
       ├─→ embedded_ui                — serve React assets from VFS
       ├─→ ConfigManager (Arc<Mutex<>>)  — SQLite CRUD
       ├─→ SessionManager (Arc<Mutex<>>)
       │    ├─→ RDPLauncher           — FreeRDP session lifecycle
       │    └─→ EmbeddingHost         — window reparenting (Phase 1: stub)
       └─→ webui_sys                  — WebUI window lifecycle FFI
```

---

## 3. Crate Layout

rdpmanager is a Cargo workspace with three crates:

```
rdpmanager/
├── Cargo.toml              # Workspace root + binary crate
├── build.rs                # VFS generation + Windows icon embedding
├── src/                    # Application source (binary crate)
├── crates/
│   ├── webui-sys/          # FFI bindings to WebUI C library
│   │   ├── Cargo.toml      # links = "webui"
│   │   ├── build.rs        # cc (C/C++/ObjC compilation) + bindgen
│   │   ├── src/lib.rs      # Re-exports generated bindings
│   │   └── wrapper.h       # #include "webui.h"
│   └── freerdp-sys/        # FFI bindings to FreeRDP3
│       ├── Cargo.toml      # links = "freerdp"
│       ├── build.rs        # cmake (build from source) + bindgen
│       ├── src/lib.rs      # Re-exports generated bindings
│       └── wrapper.h       # FreeRDP/WinPR headers
└── subprojects/
    ├── webui/              # Vendored WebUI source (git submodule)
    └── FreeRDP/            # Vendored FreeRDP3 source (git submodule)
```

### Root Crate: `rdpmanager`

- **Type**: Binary (`src/main.rs`)
- **Edition**: 2021
- **Key dependencies**: `rusqlite` 0.33, `serde`/`serde_json`, `ureq` 3, `quick-xml` 0.37, `uuid`, `log`/`env_logger`, `rfd` (Windows/macOS file dialogs), `winres` (Windows icon)
- **Internal deps**: `webui-sys` (path), `freerdp-sys` (path)

### `crates/webui-sys`

Low-level FFI crate for the [WebUI](https://webui.me/) library. The build script:

1. **Applies patches** — `patches/webui/navigate-passthrough.patch` (enables OAuth redirect passthrough for AAD auth)
2. **Compiles C source** via `cc` — `webui.c` + `civetweb.c` (embedded HTTP server), plus platform-specific WebView code:
   - Linux: WebKitGTK (handled by WebUI internally)
   - Windows: `win32_wv2.cpp` (WebView2)
   - macOS: `wkwebview.m` (WKWebView)
3. **Generates Rust bindings** via `bindgen` — allowlists `webui_*` functions, types, and constants

### `crates/freerdp-sys`

FFI crate that builds FreeRDP3 from source as a static library. The build script:

1. **Applies patches** — `patches/freerdp/aad-fallback-parse.patch` (flexible AAD token parsing)
2. **Runs CMake** with platform-specific configuration:
   - Linux: X11, AAD, FFmpeg (video/audio), camera redirection
   - Windows: Native SSPI, no X11/FFmpeg
   - macOS: No X11/FFmpeg
3. **Links static libraries** — `freerdp-client3`, `freerdp3`, `winpr-tools3`, `winpr3`, plus platform system libraries
4. **Generates Rust bindings** via `bindgen` — FreeRDP and WinPR APIs

See [PATCHES.md](../PATCHES.md) for detailed patch descriptions.

---

## 4. Module Responsibilities

| Module | File | LOC | Purpose |
|--------|------|-----|---------|
| `main` | `src/main.rs` | ~52 | Entry point: logging init, `MainWindow` lifecycle |
| `gui::main_window` | `src/gui/main_window.rs` | ~172 | WebUI window setup, VFS serving, event handling |
| `js_handlers` | `src/js_handlers.rs` | ~512 | 33 JS↔Rust bindings for all UI interactions |
| `config_manager` | `src/config_manager.rs` | ~470 | SQLite CRUD, multi-database, schema management |
| `rdp_launcher` | `src/rdp_launcher.rs` | ~360 | FreeRDP session lifecycle (Linux: full, others: stub) |
| `session_manager` | `src/session_manager.rs` | ~103 | Coordinates RDPLauncher + EmbeddingHost + tab state |
| `window_embedding` | `src/window_embedding.rs` | ~78 | Platform window reparenting (Phase 1: stub) |
| `types` | `src/types.rs` | ~208 | Domain types: `ConnectionProfile`, `SessionState`, `CertificateInfo`, etc. |
| `embedded_ui` | `src/embedded_ui.rs` | ~11 | Generated VFS for embedded React assets |
| `utils` | `src/utils.rs` | ~96 | URL encoding, JWT parsing, PKCE, UUID helpers |
| `feed_discovery` | `src/feed_discovery.rs` | ~20 | WVD/AVD feed import (stub — design documented) |
| `rdp_file_parser` | `src/rdp_file_parser.rs` | ~14 | `.rdp` file import (stub — design documented) |
| `dialog_manager` | `src/dialog_manager.rs` | ~14 | Certificate/auth dialog sync (stub — design documented) |

### Module Details

#### `main.rs` — Entry Point

```rust
fn main() {
    // 1. Initialize env_logger with millisecond timestamps
    // 2. Log version: "rdpmanager v{VERSION}"
    // 3. MainWindow::new()   → creates WebUI window + shared state
    // 4. window.initialize() → registers bindings, sets up UI serving
    // 5. window.show()       → opens browser/WebView
    // 6. MainWindow::wait()  → blocks until window closes (webui_wait)
    // 7. Log "rdpmanager exiting"
}
```

All modules are declared here:

```rust
mod config_manager;
mod dialog_manager;
mod embedded_ui;       // include!(concat!(env!("OUT_DIR"), "/embedded_ui.rs"))
mod feed_discovery;
mod gui;
mod js_handlers;
mod rdp_file_parser;
mod rdp_launcher;
mod session_manager;
mod types;
mod utils;
mod window_embedding;
```

#### `gui::main_window` — WebUI Window

`MainWindow` manages the WebUI window lifecycle:

```rust
pub struct MainWindow {
    window: usize,                              // WebUI window handle
    config_manager: Arc<Mutex<ConfigManager>>,
    session_manager: Arc<Mutex<SessionManager>>,
}
```

- **`new()`**: Creates WebUI window, initializes `ConfigManager` + `SessionManager`, wraps in `Arc<Mutex<>>`
- **`initialize()`**: Sets window size (1200×800), registers JS bindings via `js_handlers::bind_all()`, configures UI serving:
  - *Embedded mode*: If `embedded_ui::has_files()` → installs custom VFS handler (`vfs_handler`)
  - *Disk mode*: Searches for UI directory (`ui-dist/`, `src/ui-react/dist/`, `src/ui/`) → calls `webui_set_root_folder()`
- **`show()`**: Opens `index.html` via `webui_show()`, with fallback to `webui_show_browser()` (any browser)
- **`wait()`**: Calls `webui_set_timeout(0)` (wait forever) → `webui_wait()` (blocks) → `webui_clean()`

The VFS handler (`vfs_handler`) looks up paths in `embedded_ui::lookup()`, builds HTTP responses with correct `Content-Type` and `Cache-Control` headers, and returns data via `webui_malloc()`.

#### `js_handlers.rs` — JS↔Rust Bindings

Registers 33 callback functions that the React UI calls via global JavaScript functions. Organized by category:

| Category | Bindings | Count |
|----------|----------|-------|
| **Database Mgmt** | `createDatabase`, `createDatabaseDialog`, `openDatabase`, `openDatabaseDialog`, `cloneDatabase`, `closeDatabase`, `getDatabaseStatus` | 7 |
| **Connection CRUD** | `connectRDP`, `getConnections`, `saveConnection`, `deleteConnection`, `importRdpFile`, `getAppInfo` | 6 |
| **Folder Mgmt** | `createFolder`, `moveFolder`, `renameFolder`, `deleteFolder`, `getFolders` | 5 |
| **Dialog Responses** | `certificateResponse`, `authResponse`, `aadAuthResponse` | 3 (stubs) |
| **Feed Discovery** | `getFeedAccounts`, `deleteFeedAccount`, `discoverFeeds`, `logOffAccount`, `forgetAccount` | 5 (stubs) |
| **Folder Settings** | `getFolderSettings`, `saveFolderSettings`, `getEffectiveFolderSettings` | 3 |
| **Effective Profile** | `getEffectiveConnectionProfile` | 1 |
| **Session Tabs** | `switchTab`, `showHomeTab`, `resizeSession`, `disconnectSession`, `getActiveSessions` | 5 |

**State access pattern**:

```rust
static STATE: OnceLock<Arc<Mutex<ConfigManager>>> = OnceLock::new();
static SESSION_STATE: OnceLock<Arc<Mutex<SessionManager>>> = OnceLock::new();

// Helper closures for locking
fn with_config<F, R>(f: F) -> R where F: FnOnce(&mut ConfigManager) -> R { ... }
fn with_sessions<F, R>(f: F) -> R where F: FnOnce(&mut SessionManager) -> R { ... }
```

**FFI helpers**: `get_string_at(e, index)`, `return_bool(e, val)`, `return_string(e, val)` — thin wrappers around `webui_sys` FFI calls.

**Platform-specific file dialogs**:
- Windows/macOS: `rfd::FileDialog` (native dialogs)
- Linux: Falls back to `zenity` or `kdialog` via `std::process::Command`

#### `config_manager.rs` — SQLite Persistence

Manages all database operations with support for multiple database files.

```rust
pub struct ConfigManager {
    db: Option<rusqlite::Connection>,  // Only one database open at a time
    // Settings persisted to ~/.config/webui-rdp-client/settings.json
}
```

**Schema** (5 tables, auto-created via `ensure_schema()`):

```sql
CREATE TABLE connections (
    name TEXT PRIMARY KEY NOT NULL,
    profile_json TEXT NOT NULL           -- ConnectionProfile serialized as JSON
);

CREATE TABLE folders (
    path TEXT PRIMARY KEY NOT NULL       -- Hierarchical path (e.g. "Production/US-East")
);

CREATE TABLE folder_settings (
    path TEXT PRIMARY KEY NOT NULL,
    settings_json TEXT DEFAULT '{}'      -- FolderSettings for inheritance
);

CREATE TABLE "token-cache" (
    hostname TEXT NOT NULL,
    cache_kind TEXT NOT NULL,
    access_token TEXT NOT NULL,
    expires_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    PRIMARY KEY(hostname, cache_kind)
);

CREATE TABLE feed_accounts (
    id TEXT PRIMARY KEY NOT NULL,
    display_name TEXT,
    email TEXT,
    refresh_token TEXT,
    last_synced INTEGER
);
```

See [sqlite.md](sqlite.md) for detailed database schema and query documentation.

**Key methods**:

| Category | Methods |
|----------|---------|
| **Lifecycle** | `new()`, `open_database(path)`, `create_database(path)`, `clone_database(src, tgt)`, `close_database()`, `has_open_database()`, `get_database_path()` |
| **Connections** | `get_connection_json(name)`, `get_connections_json()`, `save_connection(json)`, `delete_connection(name)` |
| **Folders** | `get_folders_json()`, `create_folder(path)`, `move_folder(src, parent)`, `rename_folder(src, new_name)`, `delete_folder(path)` |
| **Folder Settings** | `get_folder_settings(path)`, `save_folder_settings(path, json)` |

**Multi-database support**: Uses `rusqlite::backup::Backup` with `step(-1)` for cloning. Settings file tracks `last_database_path` for auto-reopen.

#### `types.rs` — Domain Model

Defines 11 structs and enums used throughout the application:

**`ConnectionProfile`** — the core data type (35 fields):

```rust
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConnectionProfile {
    pub name: String,
    pub folder: String,
    pub hostname: String,
    pub port: u16,                    // default: 3389
    pub username: String,
    pub domain: String,
    pub width: u32,                   // default: 1920
    pub height: u32,                  // default: 1080
    pub fullscreen: bool,
    // Display & UX
    pub home_drive: bool,
    pub clipboard: bool,
    pub dynamic_resolution: bool,
    pub floatbar: bool,
    // Security
    pub cert_tofu: bool,             // Trust-on-first-use
    pub usb_auto: bool,
    // Network
    pub network_auto: bool,
    pub compression: bool,
    pub auto_reconnect: bool,         // default: true
    pub auto_reconnect_max_retries: i32, // default: 3
    // Codecs & media
    pub gfx_avc420: bool,
    pub audio_pulse: bool,
    pub prevent_session_lock: bool,
    // Gateway
    pub gateway_hostname: String,
    pub load_balance_info: String,
    // Azure AD / AVD
    pub enable_rds_aad_auth: bool,
    pub target_is_aad_joined: bool,
    pub use_manual_code_flow: bool,
    pub aad_tenant_id: String,
    pub wvd_endpoint_pool: String,
    pub workspace_id: String,
    pub arm_path: String,
    // Remote App
    pub remote_application_program: String,
    pub remote_desktop_name: String,
    pub source_account_id: String,
    // Inheritance
    pub overridden_fields: Vec<String>,
}
```

**Other types**:

| Type | Purpose |
|------|---------|
| `FolderSettings` | Sparse optional overrides — all fields are `Option<T>` for cascading inheritance |
| `CertificateInfo` | Host, port, CN, subject, issuer, fingerprint, `is_changed`, `old_fingerprint` |
| `CertificateAcceptance` | `Reject(0)`, `AcceptPermanently(1)`, `AcceptTemporarily(2)` |
| `AuthRequest` / `AuthResponse` | Credential dialog data (hostname, is_gateway, username, password, domain) |
| `AADAuthType` | `RdsAad`, `Avd` |
| `AADAuthRequest` / `AADAuthResponse` | OAuth popup data (auth_url, scope, redirect handling) |
| `FeedAccount` | AVD feed account (id, display_name, email, refresh_token, last_synced) |
| `RDPConnectionState` | `Idle(0)` → `Connecting(1)` → `Connected(2)` → `Disconnecting(3)` → `Disconnected(4)` / `Error(5)` |

**Callback types** for FreeRDP integration:

```rust
type CertVerifyCallback = Box<dyn Fn(&CertificateInfo) -> CertificateAcceptance + Send + Sync>;
type AuthCallback       = Box<dyn Fn(&AuthRequest) -> AuthResponse + Send + Sync>;
type AADAuthCallback    = Box<dyn Fn(&AADAuthRequest) -> AADAuthResponse + Send + Sync>;
type TokenCacheCallback = Box<dyn Fn(&str, &str) -> Option<String> + Send + Sync>;
```

#### `rdp_launcher.rs` — FreeRDP Sessions

Manages FreeRDP session lifecycles with platform-conditional compilation:

```rust
pub struct RDPLauncher {
    sessions: HashMap<String, Arc<Mutex<RDPSession>>>,
}

pub struct RDPSession {
    pub id: String,
    pub hostname: String,
    state: Arc<AtomicU8>,           // Lock-free state machine
    thread: Option<JoinHandle<()>>, // Background thread
    context_ptr: usize,             // FreeRDP context (for cleanup)
}
```

**Launch flow** (Linux only — `#[cfg(target_os = "linux")]`):

1. `RdpClientEntry(&mut entry)` — FFI call to initialize FreeRDP entry points
2. `freerdp_client_context_new(entry)` — allocate FreeRDP context
3. `apply_settings(settings, profile)` — map `ConnectionProfile` fields to FreeRDP settings via unsafe FFI macros (`set_str!()`, `set_bool!()`, `set_u32!()`)
4. `freerdp_client_start(context)` — spawn FreeRDP's internal thread
5. **Background `std::thread`**:
   - State transitions: `Idle` → `Connecting` → `Connected`
   - Calls `WaitForSingleObject(thread_handle, INFINITE)` to block until FreeRDP exits
   - State → `Disconnected`, cleanup via `freerdp_client_stop()` + `freerdp_client_context_free()`

On Windows/macOS, `launch()` returns `Err("RDP sessions only supported on Linux")`.

**State machine**: Uses `Arc<AtomicU8>` for lock-free state reads (UI polls via `getActiveSessions`).

#### `session_manager.rs` — Session Coordination

Orchestrates RDP sessions, window embedding, and tab switching:

```rust
pub struct SessionManager {
    launcher: RDPLauncher,
    embedding: EmbeddingHost,
    rects: HashMap<String, ContentRect>,  // Position/size per session tab
    active_session: Option<String>,       // Currently visible tab (None = home)
    webui_window: usize,                  // WebUI window handle
}
```

**Key methods**:

| Method | What it does |
|--------|-------------|
| `connect(profile)` | Launches RDP via `launcher.launch()`, stores `ContentRect`, sets active |
| `switch_tab(session_id, rect)` | Hides all sessions, shows target, updates position |
| `show_home()` | Hides all RDP sessions (shows home tab) |
| `resize_session(session_id, rect)` | Repositions via `EmbeddingHost` (Phase 2) |
| `disconnect(session_id)` | Calls `embedding.remove_session()` + `launcher.disconnect()` |
| `cleanup()` | Joins finished threads, syncs rects map with active sessions |

#### `window_embedding.rs` — Window Reparenting (Phase 1: Stub)

Currently all methods are no-ops. FreeRDP sessions open in separate top-level windows.

```rust
pub struct EmbeddingHost {
    webui_window: usize,
}

pub struct ContentRect {
    pub x: i32, pub y: i32,
    pub width: u32, pub height: u32,
}
```

**Phase 2 plan** (commented in source):
- **Linux**: FreeRDP's `FreeRDP_ParentWindowId` + X11 `XReparentWindow()`
- **Windows**: Win32 `SetParent()` on FreeRDP HWND
- **macOS**: NSView embedding or fallback to separate windows

#### `utils.rs` — Helper Functions

| Function | Purpose |
|----------|---------|
| `url_encode(input)` / `url_decode(input)` | URL percent-encoding via `url` crate |
| `base64url_encode(data)` / `base64url_decode(input)` | URL-safe Base64 (no padding) via `base64` crate |
| `generate_uuid()` | UUID v4 via `uuid` crate |
| `generate_code_verifier()` | 128-char PKCE code verifier (96 random bytes) |
| `jwt_extract_field(token, field)` | Parse JWT payload without verification, extract field |
| `jwt_extract_expiration(token)` | Extract `exp` claim as `i64` (epoch seconds) |
| `html_decode(input)` | Decode `&lt;`, `&gt;`, `&amp;`, `&quot;`, `&apos;` |
| `rand_byte()` | Pseudo-random byte from `SystemTime` + thread ID hash |

#### Stub Modules

These modules contain TODO comments with documented design plans:

- **`feed_discovery.rs`** — WVD/AVD feed import: OAuth2 PKCE flow, XML tenant/resource parsing, RDP content download. Will use `ureq` + `quick-xml`. See `config_manager.rs` for token caching.
- **`rdp_file_parser.rs`** — `.rdp` file import: parse key-value pairs, map to `ConnectionProfile` fields (`full address` → hostname:port, etc.).
- **`dialog_manager.rs`** — Certificate/auth dialog synchronization: `Mutex<DialogState>` + `Condvar` pattern to block FreeRDP session thread while UI collects user response. 120-second timeout to prevent deadlocks.

---

## 5. Data Flow

### Connection Lifecycle

```
┌─────────┐     ┌───────────────┐     ┌──────────────┐     ┌──────────────┐
│ React UI│────▶│ js_handlers.rs│────▶│ConfigManager │────▶│   SQLite DB  │
│         │     │               │     │              │     │              │
│ saveConn│     │ with_config() │     │save_connectn │     │ connections  │
│ ection()│     │ lock mutex    │     │ (JSON)       │     │ table        │
└─────────┘     └───────────────┘     └──────────────┘     └──────────────┘
```

### RDP Session Launch

```
1. User clicks "Connect" in React UI
2. React calls global JS function:  connectRDP('{"hostname":"server.example.com",...}')
3. WebUI dispatches to registered Rust callback in js_handlers.rs
4. Handler deserializes JSON → ConnectionProfile
5. Acquires SessionManager lock via with_sessions()
6. SessionManager::connect(profile)
   a. RDPLauncher::launch(profile)     → spawns FreeRDP thread (Linux)
   b. Stores ContentRect for tab       → session_id returned
   c. EmbeddingHost::show_session()    → (Phase 2: reparent window)
7. Returns session_id to React UI as JSON
8. React adds session tab, polls getActiveSessions() for state updates
```

### Folder Inheritance

Connection parameters cascade through the folder hierarchy:

```
Root/
├── folder_settings: { port: 3389, username: "admin" }
├── Production/
│   ├── folder_settings: { domain: "corp.example.com" }
│   └── server1                          ← inherits port=3389, username="admin", domain="corp.example.com"
└── Development/
    ├── folder_settings: { port: 3390 }
    └── dev-server                       ← inherits port=3390, username="admin" (no domain override)
```

The `getEffectiveConnectionProfile` and `getEffectiveFolderSettings` JS bindings walk the folder tree upward, merging `FolderSettings` at each level. The `overridden_fields` array on `ConnectionProfile` tracks which fields were explicitly set (vs inherited).

---

## 6. Threading Model

```
┌─────────────────────────────────────────────────────────┐
│                     Main Thread                          │
│                                                          │
│  main() → MainWindow::new() → initialize() → show()     │
│         → MainWindow::wait()                             │
│              └─ webui_wait() ←── blocks until close      │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│              WebUI Callback Threads                       │
│                                                          │
│  WebUI dispatches JS binding calls on worker threads.    │
│  Each handler acquires Arc<Mutex<ConfigManager>> or      │
│  Arc<Mutex<SessionManager>> before accessing shared      │
│  state. Mutex scope is kept minimal.                     │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│            RDP Session Threads (one per session)          │
│                                                          │
│  std::thread::spawn → {                                  │
│    state = Connecting → Connected                        │
│    WaitForSingleObject(freerdp_thread, INFINITE)         │
│    state = Disconnected                                  │
│    freerdp_client_stop() + context_free()                │
│  }                                                       │
│                                                          │
│  State tracked via Arc<AtomicU8> (lock-free reads)       │
└─────────────────────────────────────────────────────────┘
```

### Thread Safety Mechanisms

| Mechanism | Used By | Purpose |
|-----------|---------|---------|
| `OnceLock<Arc<Mutex<T>>>` | `js_handlers.rs` | One-time initialization of shared state; static lifetime |
| `Arc<Mutex<ConfigManager>>` | All DB operations | Exclusive access to SQLite connection |
| `Arc<Mutex<SessionManager>>` | Session tab ops | Exclusive access to session map + embedding |
| `Arc<AtomicU8>` | `RDPSession.state` | Lock-free state reads from UI polling |
| `JoinHandle<()>` | `RDPSession.thread` | Clean thread cleanup on disconnect/exit |

### Future: Dialog Synchronization (Phase 2)

The `dialog_manager.rs` stub documents a `Mutex<DialogState>` + `Condvar` pattern:

1. FreeRDP callback fires on session thread (cert verify / auth needed)
2. Handler sets `DialogState`, calls `condvar.notify_one()`
3. JS binding polls or is notified, shows UI dialog
4. User responds → JS calls `certificateResponse()` / `authResponse()`
5. Handler sets response in `DialogState`, calls `condvar.notify_one()`
6. Session thread wakes, reads response, continues
7. 120-second timeout prevents deadlocks if UI is unresponsive

---

## 7. Build System

### Build Pipeline

```
cargo build
  │
  ├─→ crates/freerdp-sys/build.rs
  │     ├─ git apply patches/freerdp/aad-fallback-parse.patch
  │     ├─ cmake (build FreeRDP3 from source as static libs)
  │     ├─ cargo:rustc-link-lib=static=freerdp-client3, freerdp3, winpr3, ...
  │     ├─ cargo:rustc-link-lib=dylib=ssl, crypto, X11, avcodec, ... (system)
  │     └─ bindgen → OUT_DIR/bindings.rs
  │
  ├─→ crates/webui-sys/build.rs
  │     ├─ git apply patches/webui/navigate-passthrough.patch
  │     ├─ cc (compile webui.c + civetweb.c + platform WebView)
  │     ├─ cargo:rustc-link-lib=static=webui
  │     └─ bindgen → OUT_DIR/bindings.rs
  │
  └─→ build.rs (root)
        ├─ embed_windows_icon()     → winres (Windows PE resource)
        └─ generate_embedded_ui()   → OUT_DIR/embedded_ui.rs
              ├─ Scans: ui-dist/ or src/ui-react/dist/
              ├─ Generates: EmbeddedFile structs with include_bytes!()
              └─ If no UI found: empty stub (disk-based fallback)
```

### Embedded UI Generation

The `build.rs` generates a VFS module at compile time:

```rust
// Generated embedded_ui.rs (simplified)
pub struct EmbeddedFile {
    pub path: &'static str,
    pub data: &'static [u8],
    pub mime_type: &'static str,
}

pub static FILES: &[EmbeddedFile] = &[
    EmbeddedFile { path: "/index.html", data: include_bytes!("..."), mime_type: "text/html" },
    EmbeddedFile { path: "/assets/index.js", data: include_bytes!("..."), mime_type: "application/javascript" },
    // ...
];

pub fn lookup(path: &str) -> Option<&'static EmbeddedFile> { ... }
pub fn has_files() -> bool { ... }
```

MIME types are guessed from file extensions (html, js, css, json, png, svg, woff2, ttf, etc.).

---

## 8. Platform Support

| Feature | Linux | Windows | macOS |
|---------|-------|---------|-------|
| **RDP sessions** | ✅ Full (X11 + FFmpeg) | ❌ Stub | ❌ Stub |
| **WebUI / WebView** | ✅ WebKitGTK | ✅ WebView2 | ✅ WKWebView |
| **File dialogs** | zenity / kdialog | rfd (native) | rfd (native) |
| **AAD / Entra auth** | ✅ OAuth popup | ❌ TODO | ❌ TODO |
| **Window embedding** | Phase 2: XReparentWindow | Phase 2: SetParent | Phase 2: NSView |
| **Icon embedding** | N/A | ✅ winres (PE resource) | N/A |
| **Clipboard redirect** | ✅ Via FreeRDP | N/A | N/A |
| **USB redirect** | ✅ libusb | N/A | N/A |
| **Audio** | ✅ ALSA / PulseAudio | N/A | N/A |

### Platform-Conditional Compilation

```rust
// RDP session launch — only on Linux
#[cfg(target_os = "linux")]
extern "C" { fn RdpClientEntry(entry: *mut RDP_CLIENT_ENTRY_POINTS) -> BOOL; }

#[cfg(not(target_os = "linux"))]
pub fn launch(&mut self, _profile: &ConnectionProfile) -> Result<String, String> {
    Err("RDP sessions are only supported on Linux".into())
}

// File dialogs — rfd on Windows/macOS, CLI tools on Linux
#[cfg(any(target_os = "windows", target_os = "macos"))]
fn open_database_dialog() { rfd::FileDialog::new()... }

#[cfg(target_os = "linux")]
fn open_database_dialog() { Command::new("zenity")... }
```

---

## 9. Key Dependencies

| Crate | Version | Purpose |
|-------|---------|---------|
| `rusqlite` | 0.33 | SQLite database (bundled — no system dependency) |
| `serde` / `serde_json` | latest | JSON serialization for all UI↔backend data |
| `webui-sys` | local | FFI bindings to WebUI C library |
| `freerdp-sys` | local | FFI bindings to FreeRDP3 (CMake build from source) |
| `ureq` | 3 | Blocking HTTP client for feed discovery |
| `quick-xml` | 0.37 | XML parsing for WVD/AVD feed responses |
| `uuid` | 1 | UUID v4 generation for session IDs |
| `log` / `env_logger` | 0.4 / 0.11 | Structured logging with timestamps |
| `url` | 2 | URL encoding/decoding |
| `base64` | 0.22 | Base64url encoding for JWT/PKCE |
| `sha2` | 0.10 | SHA-256 hashing (PKCE code challenge) |
| `dirs` | 6 | Platform config directory (`~/.config/`) |
| `thiserror` | 2 | Derive macro for error types |
| `rfd` | 0.15 | Native file dialogs (Windows/macOS only) |
| `winres` | 0.1 | Windows icon embedding (build-time only) |
| `cc` | 1 | C/C++/ObjC compilation (webui-sys build) |
| `cmake` | 1 | CMake integration (freerdp-sys build) |
| `bindgen` | 0.71 | C header → Rust FFI binding generation |

---

## 10. Interconnection Map

### Full Module Dependency Graph

```
main.rs
  │
  ├─→ gui::main_window::MainWindow
  │     │
  │     ├─→ js_handlers::bind_all(window, config_mgr, session_mgr)
  │     │     │
  │     │     ├─→ OnceLock<Arc<Mutex<ConfigManager>>>  (STATE)
  │     │     │     └─→ rusqlite::Connection
  │     │     │           └─→ SQLite database file (.db)
  │     │     │
  │     │     ├─→ OnceLock<Arc<Mutex<SessionManager>>>  (SESSION_STATE)
  │     │     │     ├─→ RDPLauncher
  │     │     │     │     ├─→ HashMap<String, Arc<Mutex<RDPSession>>>
  │     │     │     │     │     ├─→ std::thread (per session)
  │     │     │     │     │     ├─→ Arc<AtomicU8> (state machine)
  │     │     │     │     │     └─→ freerdp_sys FFI calls
  │     │     │     │     └─→ freerdp_sys::freerdp_client_*()
  │     │     │     │
  │     │     │     └─→ EmbeddingHost
  │     │     │           └─→ (Phase 2: platform window APIs)
  │     │     │
  │     │     ├─→ types::{ConnectionProfile, FolderSettings, SessionInfo, ...}
  │     │     ├─→ utils::{url_encode, jwt_extract_field, generate_uuid, ...}
  │     │     └─→ webui_sys::{webui_get_string_at, webui_return_string, ...}
  │     │
  │     ├─→ embedded_ui::{lookup, has_files}  (VFS serving)
  │     │     └─→ build.rs generated include_bytes!() data
  │     │
  │     └─→ webui_sys::{webui_new_window, webui_show, webui_wait, ...}
  │
  └─→ env_logger (logging init)
```

### Cross-Module Data Flow

```
                    ┌──────────────────────────────┐
                    │          types.rs             │
                    │  ConnectionProfile            │
                    │  FolderSettings               │
                    │  CertificateInfo              │
                    │  RDPConnectionState           │
                    │  SessionInfo                  │
                    └──────────┬───────────────────┘
                               │ used by all modules
          ┌────────────────────┼────────────────────┐
          │                    │                     │
          ▼                    ▼                     ▼
   config_manager.rs    rdp_launcher.rs      js_handlers.rs
   (JSON ↔ structs)     (profile → FFI)     (JSON ↔ structs)
          │                    │                     │
          ▼                    ▼                     │
      rusqlite             freerdp-sys              │
      (SQLite)          (C FFI → FreeRDP)           │
                                                     ▼
                                               webui-sys
                                            (C FFI → WebUI)
```

---

## 11. Vendored Subprojects and Patches

Two git submodules are vendored under `subprojects/`:

### FreeRDP (`subprojects/FreeRDP`)

The FreeRDP3 library provides the RDP protocol implementation. Built from source as static libraries via CMake during `cargo build`.

**Patch**: `patches/freerdp/aad-fallback-parse.patch`
- **File modified**: `libfreerdp/core/aad.c`
- **Problem**: JSON parsing failures during Azure AD authentication; `authentication_result` field type mismatch (number vs string)
- **Solution**: Adds text-based fallback parsing for malformed JSON responses, handles both numeric and string-typed `authentication_result`, and gracefully processes base64-encoded payloads

### WebUI (`subprojects/webui`)

The WebUI library provides the browser/WebView UI hosting layer with a built-in HTTP server (Civetweb).

**Patch**: `patches/webui/navigate-passthrough.patch`
- **Files modified**: `include/webui.h`, `include/webui.hpp`, `src/webui.c`
- **Problem**: WebUI's Linux WebView backend blocks all navigation events after initial page load via `webkit_policy_decision_ignore()`
- **Solution**: Adds `webui_set_navigate_passthrough(window, bool)` API that allows navigations to proceed — required for Microsoft Entra ID OAuth login which involves dozens of cross-domain redirects

See [PATCHES.md](../PATCHES.md) for full patch details. Changes to FreeRDP or WebUI behavior should always be made as patch files, not direct submodule edits.

---

## 12. Project Status

| Component | Status | Notes |
|-----------|--------|-------|
| Core UI (MainWindow, VFS) | ✅ Complete | Embedded + disk-based serving |
| JS Bindings | ~80% | 33/33 registered; dialog/feed callbacks are stubs |
| SQLite Persistence | ✅ Complete | 5 tables, multi-db, clone, folder inheritance |
| Domain Types | ✅ Complete | 11 structs/enums, full ConnectionProfile |
| RDP Sessions (Linux) | ~50% | Launch + disconnect + state tracking; no embedded windows |
| RDP Sessions (Win/Mac) | ❌ Stub | Returns error |
| Window Embedding | Phase 1 | All no-ops; Phase 2 design documented |
| Feed Discovery | ❌ Stub | Design documented in source |
| RDP File Parser | ❌ Stub | Design documented in source |
| Dialog Manager | ❌ Stub | Condvar pattern documented |
| Build System | ✅ Complete | Cargo workspace, CMake, cc, bindgen, VFS gen |

---

## Related Documentation

- [PATCHES.md](../PATCHES.md) — Detailed patch descriptions for FreeRDP and WebUI
- [MODERNIZATION.md](../MODERNIZATION.md) — C++ → Rust migration notes
- [README.md](../README.md) — Project overview, features, build instructions
