//! WebUI JavaScript ↔ Rust binding layer.
//!
//! Registers JavaScript functions with WebUI that bridge the React
//! frontend to the Rust backend. Implemented handlers delegate to
//! ConfigManager and SessionManager; unported subsystems return graceful stubs.
//!
//! Equivalent to: `src/js_handlers.cpp` / `js_handlers.hpp`

use crate::config_manager::ConfigManager;
use crate::dialog_manager::DialogManager;
use crate::feed_discovery::FeedDiscoveryManager;
use crate::gui::aad_auth_handler::AADAuthHandler;
use crate::rdp_file_parser;
use crate::session_manager::SessionManager;
use crate::types::ConnectionProfile;
use crate::window_embedding::ContentRect;
use log::{info, warn};
use std::ffi::{CStr, CString};
use std::path::PathBuf;
use std::sync::{Arc, Mutex, OnceLock};

/// Global shared state accessible from WebUI callbacks (which are C function pointers).
static STATE: OnceLock<Arc<Mutex<ConfigManager>>> = OnceLock::new();
static SESSION_STATE: OnceLock<Arc<Mutex<SessionManager>>> = OnceLock::new();
static AAD_STATE: OnceLock<Arc<AADAuthHandler>> = OnceLock::new();
static DIALOG_STATE: OnceLock<Arc<DialogManager>> = OnceLock::new();
static FEED_STATE: OnceLock<Arc<FeedDiscoveryManager>> = OnceLock::new();

/// Register all JavaScript bindings on the given WebUI window.
pub fn bind_all(
    window: usize,
    config_manager: Arc<Mutex<ConfigManager>>,
    session_manager: Arc<Mutex<SessionManager>>,
    aad_handler: Option<Arc<AADAuthHandler>>,
    dialog_manager: Arc<DialogManager>,
    feed_manager: Option<Arc<FeedDiscoveryManager>>,
) {
    STATE.get_or_init(|| config_manager);
    SESSION_STATE.get_or_init(|| session_manager);
    if let Some(aad) = aad_handler {
        AAD_STATE.get_or_init(|| aad);
    }
    DIALOG_STATE.get_or_init(|| dialog_manager);
    if let Some(fm) = feed_manager {
        FEED_STATE.get_or_init(|| fm);
    }

    let bindings: &[(&str, unsafe extern "C" fn(*mut webui_sys::webui_event_t))] = &[
        // Database management
        ("createDatabase", on_create_database),
        ("createDatabaseDialog", on_create_database_dialog),
        ("openDatabase", on_open_database),
        ("openDatabaseDialog", on_open_database_dialog),
        ("cloneDatabase", on_clone_database),
        ("closeDatabase", on_close_database),
        ("getDatabaseStatus", on_get_database_status),
        // Connection management
        ("connectRDP", on_connect_rdp),
        ("getConnections", on_get_connections),
        ("saveConnection", on_save_connection),
        ("deleteConnection", on_delete_connection),
        ("getAppInfo", on_get_app_info),
        ("importRdpFile", on_import_rdp_file),
        // Folder management
        ("createFolder", on_create_folder),
        ("moveFolder", on_move_folder),
        ("renameFolder", on_rename_folder),
        ("deleteFolder", on_delete_folder),
        ("getFolders", on_get_folders),
        // Dialog responses (RDP session callbacks)
        ("certificateResponse", on_certificate_response),
        ("authResponse", on_auth_response),
        ("aadAuthResponse", on_aad_auth_response),
        // Feed discovery
        ("getFeedAccounts", on_get_feed_accounts),
        ("deleteFeedAccount", on_delete_feed_account),
        ("discoverFeeds", on_discover_feeds),
        ("logOffAccount", on_log_off_account),
        ("forgetAccount", on_forget_account),
        // Folder settings & inheritance
        ("getFolderSettings", on_get_folder_settings),
        ("saveFolderSettings", on_save_folder_settings),
        (
            "getEffectiveFolderSettings",
            on_get_effective_folder_settings,
        ),
        (
            "getEffectiveConnectionProfile",
            on_get_effective_connection_profile,
        ),
        // Session management (tab embedding)
        ("switchTab", on_switch_tab),
        ("showHomeTab", on_show_home_tab),
        ("resizeSession", on_resize_session),
        ("disconnectSession", on_disconnect_session),
        ("getActiveSessions", on_get_active_sessions),
    ];

    for (name, handler) in bindings {
        let cname = CString::new(*name).unwrap();
        unsafe {
            webui_sys::webui_bind(window, cname.as_ptr(), Some(*handler));
        }
    }
    info!("Registered {} JS bindings", bindings.len());
}

// ── Helpers ──────────────────────────────────────────────────────────────

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

unsafe fn get_string_at(e: *mut webui_sys::webui_event_t, index: usize) -> String {
    let ptr = unsafe { webui_sys::webui_get_string_at(e, index) };
    if ptr.is_null() {
        return String::new();
    }
    unsafe { CStr::from_ptr(ptr) }
        .to_str()
        .unwrap_or("")
        .to_string()
}

unsafe fn return_bool(e: *mut webui_sys::webui_event_t, val: bool) {
    unsafe { webui_sys::webui_return_bool(e, val) };
}

unsafe fn return_string(e: *mut webui_sys::webui_event_t, val: &str) {
    let cs = CString::new(val).unwrap_or_default();
    unsafe { webui_sys::webui_return_string(e, cs.as_ptr()) };
}

// ── File dialog helpers (delegated to file_dialogs module) ───────────────

fn pick_save_file(title: &str, default_name: &str) -> Option<PathBuf> {
    crate::file_dialogs::pick_save_file(title, default_name)
}

fn pick_open_file(title: &str) -> Option<PathBuf> {
    crate::file_dialogs::pick_open_file(title)
}

// ── Database handlers ────────────────────────────────────────────────────

unsafe extern "C" fn on_create_database(e: *mut webui_sys::webui_event_t) {
    let path = unsafe { get_string_at(e, 0) };
    let ok = with_config(|cm| cm.create_database(&path)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_create_database_dialog(e: *mut webui_sys::webui_event_t) {
    let Some(p) = pick_save_file("Create Database", "connections.db") else {
        unsafe { return_bool(e, false) };
        return;
    };
    let path_str = p.to_string_lossy().to_string();
    info!("Create database dialog: {path_str}");
    let ok = with_config(|cm| cm.create_database(&path_str)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_open_database(e: *mut webui_sys::webui_event_t) {
    let path = unsafe { get_string_at(e, 0) };
    let ok = with_config(|cm| cm.open_database(&path)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_open_database_dialog(e: *mut webui_sys::webui_event_t) {
    let Some(p) = pick_open_file("Open Database") else {
        unsafe { return_bool(e, false) };
        return;
    };
    let path_str = p.to_string_lossy().to_string();
    info!("Open database dialog: {path_str}");
    let ok = with_config(|cm| cm.open_database(&path_str)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_clone_database(e: *mut webui_sys::webui_event_t) {
    let source = unsafe { get_string_at(e, 0) };
    let target = unsafe { get_string_at(e, 1) };
    let ok = with_config(|cm| cm.clone_database(&source, &target)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_close_database(e: *mut webui_sys::webui_event_t) {
    let ok = with_config(|cm| cm.close_database()).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_get_database_status(e: *mut webui_sys::webui_event_t) {
    let result = with_config(|cm| {
        let is_open = cm.has_open_database();
        let path = cm
            .get_database_path()
            .replace('\\', "\\\\")
            .replace('"', "\\\"");
        format!(r#"{{"isOpen":{},"path":"{}"}}"#, is_open, path)
    })
    .unwrap_or_else(|| r#"{"isOpen":false,"path":""}"#.to_string());
    unsafe { return_string(e, &result) };
}

// ── Connection management ────────────────────────────────────────────────

unsafe extern "C" fn on_connect_rdp(e: *mut webui_sys::webui_event_t) {
    let json_str = unsafe { get_string_at(e, 0) };
    if json_str.is_empty() {
        unsafe {
            return_string(
                e,
                r#"{"success":false,"error":"Empty connection parameters"}"#,
            )
        };
        return;
    }

    // Parse connection profile
    let profile: ConnectionProfile = match serde_json::from_str(&json_str) {
        Ok(v) => v,
        Err(err) => {
            let msg = format!(
                r#"{{"success":false,"error":"Invalid JSON: {}"}}"#,
                err.to_string().replace('"', "\\\"")
            );
            unsafe { return_string(e, &msg) };
            return;
        }
    };

    if profile.hostname.is_empty() {
        unsafe { return_string(e, r#"{"success":false,"error":"Hostname cannot be empty"}"#) };
        return;
    }

    info!("connectRDP: launching session to {}", profile.hostname);

    // Default content rect (will be updated by switchTab)
    let rect = ContentRect {
        x: 0,
        y: 0,
        width: profile.width,
        height: profile.height,
    };

    let result = with_sessions(|sm| sm.connect(&profile, rect));

    match result {
        Some(Ok(session_id)) => {
            let msg = format!(
                r#"{{"success":true,"sessionId":"{}"}}"#,
                session_id.replace('"', "\\\"")
            );
            unsafe { return_string(e, &msg) };
        }
        Some(Err(err)) => {
            let msg = format!(
                r#"{{"success":false,"error":"{}"}}"#,
                err.replace('"', "\\\"")
            );
            unsafe { return_string(e, &msg) };
        }
        None => {
            unsafe {
                return_string(
                    e,
                    r#"{"success":false,"error":"Session manager not initialized"}"#,
                )
            };
        }
    }
}

unsafe extern "C" fn on_get_connections(e: *mut webui_sys::webui_event_t) {
    let json = with_config(|cm| cm.get_connections_json()).unwrap_or_else(|| "[]".to_string());
    unsafe { return_string(e, &json) };
}

unsafe extern "C" fn on_save_connection(e: *mut webui_sys::webui_event_t) {
    let json_str = unsafe { get_string_at(e, 0) };
    let ok = with_config(|cm| cm.save_connection(&json_str)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_delete_connection(e: *mut webui_sys::webui_event_t) {
    let name = unsafe { get_string_at(e, 0) };
    let ok = with_config(|cm| cm.delete_connection(&name)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_get_app_info(e: *mut webui_sys::webui_event_t) {
    let version = env!("CARGO_PKG_VERSION");
    let json = format!(
        r#"{{"name":"rdpmanager","version":"{version}","freerdp_version":"3.x (Rust build – launcher pending)"}}"#
    );
    unsafe { return_string(e, &json) };
}

unsafe extern "C" fn on_import_rdp_file(e: *mut webui_sys::webui_event_t) {
    let content = unsafe { get_string_at(e, 0) };
    match rdp_file_parser::parse_content(&content) {
        Ok(data) => {
            let profile = data.to_connection_profile();
            let name = profile.name.clone();
            match serde_json::to_string(&profile) {
                Ok(json) => {
                    let ok = with_config(|cm| cm.save_connection(&json)).unwrap_or(false);
                    if ok {
                        let resp = serde_json::json!({"success": true, "name": name});
                        unsafe { return_string(e, &resp.to_string()) };
                    } else {
                        unsafe {
                            return_string(
                                e,
                                r#"{"success":false,"error":"Failed to save connection"}"#,
                            )
                        };
                    }
                }
                Err(err) => {
                    let resp = serde_json::json!({"success": false, "error": err.to_string()});
                    unsafe { return_string(e, &resp.to_string()) };
                }
            }
        }
        Err(err) => {
            let resp = serde_json::json!({"success": false, "error": err});
            unsafe { return_string(e, &resp.to_string()) };
        }
    }
}

// ── Folder management ────────────────────────────────────────────────────

unsafe extern "C" fn on_create_folder(e: *mut webui_sys::webui_event_t) {
    let path = unsafe { get_string_at(e, 0) };
    let ok = with_config(|cm| cm.create_folder(&path)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_move_folder(e: *mut webui_sys::webui_event_t) {
    let source = unsafe { get_string_at(e, 0) };
    let target_parent = unsafe { get_string_at(e, 1) };
    let ok = with_config(|cm| cm.move_folder(&source, &target_parent)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_rename_folder(e: *mut webui_sys::webui_event_t) {
    let source = unsafe { get_string_at(e, 0) };
    let new_name = unsafe { get_string_at(e, 1) };
    let ok = with_config(|cm| cm.rename_folder(&source, &new_name)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_delete_folder(e: *mut webui_sys::webui_event_t) {
    let path = unsafe { get_string_at(e, 0) };
    let ok = with_config(|cm| cm.delete_folder(&path)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_get_folders(e: *mut webui_sys::webui_event_t) {
    let json = with_config(|cm| cm.get_folders_json()).unwrap_or_else(|| "[]".to_string());
    unsafe { return_string(e, &json) };
}

// ── Dialog responses ─────────────────────────────────────────────────────

unsafe extern "C" fn on_certificate_response(e: *mut webui_sys::webui_event_t) {
    let choice = unsafe { webui_sys::webui_get_int_at(e, 0) } as i32;
    if let Some(dm) = DIALOG_STATE.get() {
        dm.on_certificate_response(choice);
    } else {
        warn!("certificateResponse: DialogManager not initialized");
    }
}

unsafe extern "C" fn on_auth_response(e: *mut webui_sys::webui_event_t) {
    let success = unsafe { webui_sys::webui_get_bool_at(e, 0) };
    let username = unsafe { get_string_at(e, 1) };
    let password = unsafe { get_string_at(e, 2) };
    let domain = unsafe { get_string_at(e, 3) };
    if let Some(dm) = DIALOG_STATE.get() {
        dm.on_auth_response(success, &username, &password, &domain);
    } else {
        warn!("authResponse: DialogManager not initialized");
    }
}

unsafe extern "C" fn on_aad_auth_response(e: *mut webui_sys::webui_event_t) {
    let success = unsafe { webui_sys::webui_get_bool_at(e, 0) };
    let redirect_url = unsafe { get_string_at(e, 1) };
    if let Some(aad) = AAD_STATE.get() {
        aad.on_response(success, &redirect_url);
    } else {
        warn!("aadAuthResponse: AADAuthHandler not registered");
    }
}

// ── Feed discovery ────────────────────────────────────────────────────────

unsafe extern "C" fn on_get_feed_accounts(e: *mut webui_sys::webui_event_t) {
    let json = with_config(|cm| cm.get_feed_accounts_json()).unwrap_or_else(|| "[]".to_string());
    unsafe { return_string(e, &json) };
}

unsafe extern "C" fn on_delete_feed_account(e: *mut webui_sys::webui_event_t) {
    let id = unsafe { get_string_at(e, 0) };
    let ok = with_config(|cm| cm.delete_feed_account(&id)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_discover_feeds(e: *mut webui_sys::webui_event_t) {
    let account_id = unsafe { get_string_at(e, 0) };
    if let Some(fm) = FEED_STATE.get() {
        if fm.is_busy() {
            unsafe {
                return_string(
                    e,
                    r#"{"success":false,"error":"Feed discovery already in progress"}"#,
                )
            };
            return;
        }
        let fm = Arc::clone(fm);
        std::thread::spawn(move || {
            fm.discover_and_import(&account_id);
        });
        unsafe { return_string(e, r#"{"success":true,"message":"Discovery started"}"#) };
    } else {
        warn!("discoverFeeds: FeedDiscoveryManager not initialized");
        unsafe {
            return_string(
                e,
                r#"{"success":false,"error":"Feed discovery not initialized"}"#,
            )
        };
    }
}

unsafe extern "C" fn on_log_off_account(e: *mut webui_sys::webui_event_t) {
    let id = unsafe { get_string_at(e, 0) };
    let ok = with_config(|cm| cm.clear_tokens_for_account(&id)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_forget_account(e: *mut webui_sys::webui_event_t) {
    let id = unsafe { get_string_at(e, 0) };
    let ok = with_config(|cm| cm.forget_account(&id)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

// ── Folder settings ──────────────────────────────────────────────────────

unsafe extern "C" fn on_get_folder_settings(e: *mut webui_sys::webui_event_t) {
    let path = unsafe { get_string_at(e, 0) };
    let json = with_config(|cm| cm.get_folder_settings(&path)).unwrap_or_else(|| "{}".to_string());
    unsafe { return_string(e, &json) };
}

unsafe extern "C" fn on_save_folder_settings(e: *mut webui_sys::webui_event_t) {
    let path = unsafe { get_string_at(e, 0) };
    let settings_json = unsafe { get_string_at(e, 1) };
    let ok = with_config(|cm| cm.save_folder_settings(&path, &settings_json)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_get_effective_folder_settings(e: *mut webui_sys::webui_event_t) {
    let path = unsafe { get_string_at(e, 0) };
    let json = with_config(|cm| cm.get_effective_folder_settings(&path))
        .unwrap_or_else(|| "{}".to_string());
    unsafe { return_string(e, &json) };
}

unsafe extern "C" fn on_get_effective_connection_profile(e: *mut webui_sys::webui_event_t) {
    let name = unsafe { get_string_at(e, 0) };
    let json = with_config(|cm| cm.get_effective_connection_profile(&name))
        .flatten()
        .unwrap_or_else(|| "null".to_string());
    unsafe { return_string(e, &json) };
}

// ── Session management (tab embedding) ───────────────────────────────────

unsafe extern "C" fn on_switch_tab(e: *mut webui_sys::webui_event_t) {
    let json_str = unsafe { get_string_at(e, 0) };
    let val: serde_json::Value = match serde_json::from_str(&json_str) {
        Ok(v) => v,
        Err(_) => {
            unsafe { return_bool(e, false) };
            return;
        }
    };

    let session_id = val.get("sessionId").and_then(|v| v.as_str()).unwrap_or("");
    if session_id.is_empty() {
        unsafe { return_bool(e, false) };
        return;
    }

    let rect = ContentRect {
        x: val.get("x").and_then(|v| v.as_i64()).unwrap_or(0) as i32,
        y: val.get("y").and_then(|v| v.as_i64()).unwrap_or(0) as i32,
        width: val.get("width").and_then(|v| v.as_u64()).unwrap_or(800) as u32,
        height: val.get("height").and_then(|v| v.as_u64()).unwrap_or(600) as u32,
    };

    let ok = with_sessions(|sm| sm.switch_tab(session_id, rect)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_show_home_tab(e: *mut webui_sys::webui_event_t) {
    with_sessions(|sm| sm.show_home());
    unsafe { return_bool(e, true) };
}

unsafe extern "C" fn on_resize_session(e: *mut webui_sys::webui_event_t) {
    let json_str = unsafe { get_string_at(e, 0) };
    let val: serde_json::Value = match serde_json::from_str(&json_str) {
        Ok(v) => v,
        Err(_) => {
            unsafe { return_bool(e, false) };
            return;
        }
    };

    let session_id = val.get("sessionId").and_then(|v| v.as_str()).unwrap_or("");
    if session_id.is_empty() {
        unsafe { return_bool(e, false) };
        return;
    }

    let rect = ContentRect {
        x: val.get("x").and_then(|v| v.as_i64()).unwrap_or(0) as i32,
        y: val.get("y").and_then(|v| v.as_i64()).unwrap_or(0) as i32,
        width: val.get("width").and_then(|v| v.as_u64()).unwrap_or(800) as u32,
        height: val.get("height").and_then(|v| v.as_u64()).unwrap_or(600) as u32,
    };

    with_sessions(|sm| sm.resize_session(session_id, rect));
    unsafe { return_bool(e, true) };
}

unsafe extern "C" fn on_disconnect_session(e: *mut webui_sys::webui_event_t) {
    let session_id = unsafe { get_string_at(e, 0) };
    if session_id.is_empty() {
        unsafe { return_bool(e, false) };
        return;
    }

    let ok = with_sessions(|sm| sm.disconnect(&session_id)).unwrap_or(false);
    unsafe { return_bool(e, ok) };
}

unsafe extern "C" fn on_get_active_sessions(e: *mut webui_sys::webui_event_t) {
    let sessions = with_sessions(|sm| {
        sm.cleanup();
        sm.get_sessions()
    })
    .unwrap_or_default();

    let json = serde_json::to_string(&sessions).unwrap_or_else(|_| "[]".to_string());
    unsafe { return_string(e, &json) };
}
