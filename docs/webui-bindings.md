# WebUI Integration & JS↔Rust Bindings

> How the React frontend communicates with the Rust backend through
> WebUI's JavaScript binding layer.

## 1. Overview

[WebUI](https://webui.me/) is a lightweight C library that creates a native
window containing an embedded WebView (WebView2 on Windows, WKWebView on
macOS, GTK WebView on Linux). The Rust backend talks to it through the
`webui-sys` FFI crate (`crates/webui-sys/`), which uses **bindgen** to
generate raw bindings from the vendored C headers.

**Communication flow:**

```
React component
  → calls global async function (e.g. `await connectRDP(json)`)
    → WebUI JS runtime serialises args, dispatches to native side
      → C callback (`unsafe extern "C" fn`) runs on a WebUI worker thread
        → handler accesses shared state via Arc<Mutex<T>>
          → result string returned to JS promise
```

Key points:

- Functions are registered with `webui_bind()` during initialisation.
- Each bound name becomes a **global async function** in the browser context.
- Arguments are positional strings; return values are strings or booleans.
- JSON is the standard serialisation format for complex data.

### Relevant source files

| File | Purpose |
|------|---------|
| `src/js_handlers.rs` | 35 binding handlers + helpers |
| `src/gui/main_window.rs` | Window lifecycle (create → show → wait) |
| `src/ui-react/src/webui.d.ts` | TypeScript declarations for bound functions |
| `crates/webui-sys/` | Raw FFI crate (compiled from vendored WebUI source) |

---

## 2. WebUI Lifecycle (`src/gui/main_window.rs`)

The application lifecycle is driven from `main.rs` through four phases:

```text
MainWindow::new()
  → webui_new_window()              create native window handle
  → ConfigManager::new()            open / create SQLite database
  → SessionManager::new(window)     initialise RDP session tracking

MainWindow::initialize()
  → webui_set_size(window, 1200, 800)
  → webui_bind(window, "", on_event)            all-events handler (disconnect detection)
  → js_handlers::bind_all(window, cm, sm)       register 35 JS↔Rust bindings
  → embedded_ui::has_files()?
      true  → webui_set_file_handler(vfs_handler)   serve from compiled-in VFS
      false → webui_set_root_folder(ui_path)         serve from disk (dev mode)

MainWindow::show()
  → webui_show(window, "index.html")
  → on failure: webui_show_browser(window, "index.html", 1)   browser fallback

MainWindow::wait()
  → webui_set_timeout(0)            wait forever
  → webui_wait()                    BLOCKS until window closes
  → webui_clean()                   release resources
```

The **disconnect handler** ensures a clean shutdown when the WebView closes:

```rust
unsafe extern "C" fn on_event(e: *mut webui_sys::webui_event_t) {
    let event = unsafe { &*e };
    if event.event_type == 0 {          // WEBUI_EVENT_DISCONNECTED
        info!("Window disconnected — signalling exit");
        unsafe { webui_sys::webui_exit(); }
    }
}
```

---

## 3. Binding Registration Pattern

All 35 bindings are registered in `js_handlers::bind_all()`:

```rust
pub fn bind_all(
    window: usize,
    config_manager: Arc<Mutex<ConfigManager>>,
    session_manager: Arc<Mutex<SessionManager>>,
) {
    // Store shared state in process-wide OnceLock so C callbacks can reach it
    STATE.get_or_init(|| config_manager);
    SESSION_STATE.get_or_init(|| session_manager);

    let bindings: &[(&str, unsafe extern "C" fn(*mut webui_sys::webui_event_t))] = &[
        ("createDatabase",  on_create_database),
        ("connectRDP",      on_connect_rdp),
        // ... 33 more ...
    ];

    for (name, handler) in bindings {
        let cname = CString::new(*name).unwrap();
        unsafe {
            webui_sys::webui_bind(window, cname.as_ptr(), Some(*handler));
        }
    }
    info!("Registered {} JS bindings", bindings.len());
}
```

Each handler follows a consistent three-step pattern:

```rust
unsafe extern "C" fn on_create_database(e: *mut webui_sys::webui_event_t) {
    // 1. Extract arguments from JS call
    let path = unsafe { get_string_at(e, 0) };

    // 2. Delegate to business logic through shared state
    let ok = with_config(|cm| cm.create_database(&path)).unwrap_or(false);

    // 3. Return result to the JS caller
    unsafe { return_bool(e, ok) };
}
```

### Helper functions

| Helper | Signature | Purpose |
|--------|-----------|---------|
| `get_string_at` | `(e, index) → String` | Extract the *n*-th string argument from the JS call |
| `return_bool` | `(e, val)` | Return a boolean to the JS promise |
| `return_string` | `(e, val)` | Return a string to the JS promise |

These are thin wrappers around `webui_sys::webui_get_string_at`,
`webui_sys::webui_return_bool`, and `webui_sys::webui_return_string`,
handling `CStr`/`CString` conversion.

---

## 4. State Access Pattern

WebUI callbacks are plain C function pointers — they cannot capture
environment. Shared state is therefore stored in `static` globals and
accessed through lock helpers:

```rust
use std::sync::{Arc, Mutex, OnceLock};

static STATE:         OnceLock<Arc<Mutex<ConfigManager>>>  = OnceLock::new();
static SESSION_STATE: OnceLock<Arc<Mutex<SessionManager>>> = OnceLock::new();
```

- **`OnceLock`** guarantees single initialisation (set once in `bind_all`).
- **`Arc<Mutex<T>>`** enables thread-safe shared access — WebUI dispatches
  callbacks on worker threads, not the main thread.

Lock helpers return `Option<R>`, gracefully handling poisoned mutexes:

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

fn with_sessions<F, R>(f: F) -> Option<R>
where
    F: FnOnce(&mut SessionManager) -> R,
{
    SESSION_STATE.get().and_then(|arc| {
        let mut guard = arc.lock().ok()?;
        Some(f(&mut guard))
    })
}
```

> **Thread-safety note:** Keep critical sections short. Long-running
> operations (RDP connect, feed discovery) should release the lock before
> blocking.

---

## 5. Complete Binding Reference (35 total)

### Database Operations (7)

| JS Function | Handler | Args | Returns |
|---|---|---|---|
| `createDatabase` | `on_create_database` | `path: string` | `boolean` |
| `createDatabaseDialog` | `on_create_database_dialog` | *(none)* | `boolean` |
| `openDatabase` | `on_open_database` | `path: string` | `boolean` |
| `openDatabaseDialog` | `on_open_database_dialog` | *(none)* | `boolean` |
| `cloneDatabase` | `on_clone_database` | `source: string, target: string` | `boolean` |
| `closeDatabase` | `on_close_database` | *(none)* | `boolean` |
| `getDatabaseStatus` | `on_get_database_status` | *(none)* | JSON: `{isOpen, path}` |

The `*Dialog` variants open a native file picker (see §7) then delegate to
the same `ConfigManager` method as the non-dialog version.

### Connection Operations (6)

| JS Function | Handler | Args | Returns |
|---|---|---|---|
| `connectRDP` | `on_connect_rdp` | `jsonParams: string` | JSON: `{success, sessionId}` or `{error}` |
| `getConnections` | `on_get_connections` | *(none)* | JSON array of `ConnectionProfile` |
| `saveConnection` | `on_save_connection` | `jsonParams: string` | `boolean` |
| `deleteConnection` | `on_delete_connection` | `name: string` | `boolean` |
| `getAppInfo` | `on_get_app_info` | *(none)* | JSON: `{name, version, freerdp_version}` |
| `importRdpFile` | `on_import_rdp_file` | `content: string` | JSON *(STUB)* |

### Folder Operations (5)

| JS Function | Handler | Args | Returns |
|---|---|---|---|
| `createFolder` | `on_create_folder` | `path: string` | `boolean` |
| `moveFolder` | `on_move_folder` | `sourcePath: string, targetParentPath: string` | `boolean` |
| `renameFolder` | `on_rename_folder` | `sourcePath: string, newName: string` | `boolean` |
| `deleteFolder` | `on_delete_folder` | `path: string` | `boolean` |
| `getFolders` | `on_get_folders` | *(none)* | JSON array of strings |

### Dialog Responses (3) — STUBS

These will be called by the React UI to respond to certificate/auth prompts
surfaced during an RDP session. Currently stubbed pending `DialogManager`
implementation.

| JS Function | Handler | Args | Returns |
|---|---|---|---|
| `certificateResponse` | `on_certificate_response` | `choice: number` | `void` |
| `authResponse` | `on_auth_response` | `success: boolean, username: string, password: string, domain: string` | `void` |
| `aadAuthResponse` | `on_aad_auth_response` | `success: boolean, redirectUrl: string` | `void` |

### Feed Discovery (5) — STUBS

Azure Virtual Desktop / Windows Virtual Desktop feed import.
Stubbed pending `FeedDiscoveryManager` port.

| JS Function | Handler | Args | Returns |
|---|---|---|---|
| `getFeedAccounts` | `on_get_feed_accounts` | *(none)* | JSON array |
| `deleteFeedAccount` | `on_delete_feed_account` | `id: string` | `boolean` |
| `discoverFeeds` | `on_discover_feeds` | `accountId: string` | JSON |
| `logOffAccount` | `on_log_off_account` | `id: string` | `boolean` |
| `forgetAccount` | `on_forget_account` | `id: string` | `boolean` |

### Folder Settings (4)

Parameter inheritance: child folders/connections inherit settings from
parent folders up to root. See [session-tabs.md](session-tabs.md) for more
on the inheritance model.

| JS Function | Handler | Args | Returns |
|---|---|---|---|
| `getFolderSettings` | `on_get_folder_settings` | `path: string` | JSON |
| `saveFolderSettings` | `on_save_folder_settings` | `path: string, settingsJson: string` | `boolean` |
| `getEffectiveFolderSettings` | `on_get_effective_folder_settings` | `path: string` | JSON (merged up to root) |
| `getEffectiveConnectionProfile` | `on_get_effective_connection_profile` | `name: string` | JSON (fully resolved profile) |

### Session Management (5)

Tab-embedded RDP sessions — coordinates FreeRDP window positioning within
the WebView area.

| JS Function | Handler | Args | Returns |
|---|---|---|---|
| `switchTab` | `on_switch_tab` | JSON: `{sessionId, x, y, width, height}` | `boolean` |
| `showHomeTab` | `on_show_home_tab` | *(none)* | `boolean` |
| `resizeSession` | `on_resize_session` | JSON: `{sessionId, x, y, width, height}` | `boolean` |
| `disconnectSession` | `on_disconnect_session` | `sessionId: string` | `boolean` |
| `getActiveSessions` | `on_get_active_sessions` | *(none)* | JSON array of `SessionInfo` |

---

## 6. TypeScript Declarations (`src/ui-react/src/webui.d.ts`)

Every bound function is declared as a **global async function** so that
React components can call them without imports:

```typescript
// WebUI runtime object (injected by webui.js)
interface WebUI {
  call(fn: string, ...args: unknown[]): Promise<unknown>;
}
declare const webui: WebUI;

// ── Database ──
declare function createDatabase(path: string): Promise<boolean>;
declare function createDatabaseDialog(): Promise<boolean>;
declare function openDatabase(path: string): Promise<boolean>;
declare function openDatabaseDialog(): Promise<boolean>;
declare function cloneDatabase(sourcePath: string, targetPath: string): Promise<boolean>;
declare function closeDatabase(): Promise<boolean>;
declare function getDatabaseStatus(): Promise<string>;

// ── Connections ──
declare function connectRDP(jsonParams: string): Promise<string>;
declare function getConnections(): Promise<string>;
declare function saveConnection(jsonParams: string): Promise<boolean>;
declare function deleteConnection(name: string): Promise<boolean>;
declare function getAppInfo(): Promise<string>;
declare function importRdpFile(content: string): Promise<string>;

// ── Folders ──
declare function createFolder(path: string): Promise<boolean>;
declare function moveFolder(sourcePath: string, targetParentPath: string): Promise<boolean>;
declare function renameFolder(sourcePath: string, newName: string): Promise<boolean>;
declare function deleteFolder(path: string): Promise<boolean>;
declare function getFolders(): Promise<string>;

// ── Dialog Responses ──
declare function certificateResponse(choice: number): Promise<void>;
declare function authResponse(
  success: boolean, username: string, password: string, domain: string
): Promise<void>;
declare function aadAuthResponse(success: boolean, redirectUrl: string): Promise<void>;

// ── Feed Discovery ──
declare function getFeedAccounts(): Promise<string>;
declare function deleteFeedAccount(id: string): Promise<boolean>;
declare function discoverFeeds(accountId: string): Promise<string>;
declare function logOffAccount(id: string): Promise<boolean>;
declare function forgetAccount(id: string): Promise<boolean>;

// ── Folder Settings ──
declare function getFolderSettings(path: string): Promise<string>;
declare function saveFolderSettings(path: string, settingsJson: string): Promise<boolean>;
declare function getEffectiveFolderSettings(path: string): Promise<string>;
declare function getEffectiveConnectionProfile(name: string): Promise<string>;

// ── Session Management ──
declare function switchTab(jsonParams: string): Promise<boolean>;
declare function showHomeTab(): Promise<boolean>;
declare function resizeSession(jsonParams: string): Promise<boolean>;
declare function disconnectSession(sessionId: string): Promise<boolean>;
declare function getActiveSessions(): Promise<string>;
```

Usage from a React component:

```tsx
const status = JSON.parse(await getDatabaseStatus());
if (!status.isOpen) {
  await openDatabaseDialog();
}
const connections = JSON.parse(await getConnections());
```

---

## 7. File Dialog Platform Handling

Native file dialogs are used by `createDatabaseDialog` and
`openDatabaseDialog`. The implementation is platform-conditional:

### Windows & macOS — `rfd` crate

```rust
#[cfg(not(target_os = "linux"))]
fn pick_save_file(title: &str, default_name: &str) -> Option<PathBuf> {
    rfd::FileDialog::new()
        .set_title(title)
        .set_file_name(default_name)
        .add_filter("Database Files", &["db", "sqlite", "sqlite3"])
        .add_filter("All Files", &["*"])
        .save_file()
}

#[cfg(not(target_os = "linux"))]
fn pick_open_file(title: &str) -> Option<PathBuf> {
    rfd::FileDialog::new()
        .set_title(title)
        .add_filter("Database Files", &["db", "sqlite", "sqlite3"])
        .add_filter("All Files", &["*"])
        .pick_file()
}
```

### Linux — CLI fallback (zenity → kdialog)

On Linux the `rfd` crate is not compiled. Instead the handlers shell out to
desktop environment tools:

1. **zenity** (GNOME) — tried first
2. **kdialog** (KDE) — fallback if zenity is unavailable

```rust
#[cfg(target_os = "linux")]
fn pick_save_file(title: &str, default_name: &str) -> Option<PathBuf> {
    // Try zenity
    let output = Command::new("zenity")
        .args(["--file-selection", "--save", "--confirm-overwrite",
               &format!("--title={title}"),
               &format!("--filename={default_name}")])
        .output().ok()?;
    if output.status.success() { /* return path */ }

    // Fallback to kdialog
    let output = Command::new("kdialog")
        .args(["--getsavefilename", "~",
               "*.db *.sqlite *.sqlite3|Database files"])
        .output().ok()?;
    if output.status.success() { /* return path */ }

    None
}
```

Filter extensions across all platforms: `*.db`, `*.sqlite`, `*.sqlite3`.

---

## 8. Adding a New Binding (How-To)

Follow these four steps to expose a new Rust function to the React frontend.

### Step 1 — Add the handler in `src/js_handlers.rs`

```rust
unsafe extern "C" fn on_my_new_function(e: *mut webui_sys::webui_event_t) {
    let arg = unsafe { get_string_at(e, 0) };

    let result = with_config(|cm| {
        // ... business logic ...
        serde_json::to_string(&data).unwrap_or_default()
    })
    .unwrap_or_default();

    unsafe { return_string(e, &result) };
}
```

### Step 2 — Register in `bind_all()`

Add the entry to the `bindings` array in `bind_all()`:

```rust
let bindings: &[(&str, unsafe extern "C" fn(*mut webui_sys::webui_event_t))] = &[
    // ... existing bindings ...
    ("myNewFunction", on_my_new_function),   // ← add here
];
```

The string name (`"myNewFunction"`) becomes the global JS function name.

### Step 3 — Declare in TypeScript (`src/ui-react/src/webui.d.ts`)

```typescript
declare function myNewFunction(arg: string): Promise<string>;
```

### Step 4 — Call from React

```tsx
const result = JSON.parse(await myNewFunction(JSON.stringify(payload)));
```

### Checklist

- [ ] Handler added in `js_handlers.rs` with proper error handling
- [ ] Binding registered in `bind_all()` array
- [ ] TypeScript declaration added in `webui.d.ts`
- [ ] React component calls the function and handles the response
- [ ] JSON used for complex args/returns (simple values can use bool/string)
- [ ] Thread safety considered (keep mutex locks short)

---

## Cross-References

- [embedded-assets.md](embedded-assets.md) — How React assets are compiled
  into the binary via VFS
- [session-tabs.md](session-tabs.md) — Tab embedding and FreeRDP window
  management
- [README.md](README.md) — Project overview and docs index
- [`PATCHES.md`](../PATCHES.md) — WebUI and FreeRDP patches applied to
  vendored submodules
