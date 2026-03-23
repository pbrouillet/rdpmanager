//! WebUI JavaScript ↔ Rust binding layer.
//!
//! Registers JavaScript functions with WebUI that bridge the React
//! frontend to the Rust backend. Each binding extracts parameters from
//! the WebUI event, delegates to the appropriate manager, and returns
//! a response.
//!
//! Equivalent to: `src/js_handlers.cpp` / `js_handlers.hpp`

use crate::config_manager::ConfigManager;
use log::info;
use std::ffi::{CStr, CString};
use std::path::PathBuf;
use std::sync::{Arc, Mutex, OnceLock};

/// Global shared state accessible from WebUI callbacks (which are C function pointers).
static STATE: OnceLock<Arc<Mutex<ConfigManager>>> = OnceLock::new();

/// Register all JavaScript bindings on the given WebUI window.
pub fn bind_all(window: usize, config_manager: Arc<Mutex<ConfigManager>>) {
    STATE.get_or_init(|| config_manager);

    let bindings: &[(&str, unsafe extern "C" fn(*mut webui_sys::webui_event_t))] = &[
        ("createDatabase", on_create_database),
        ("createDatabaseDialog", on_create_database_dialog),
        ("openDatabase", on_open_database),
        ("openDatabaseDialog", on_open_database_dialog),
        ("cloneDatabase", on_clone_database),
        ("closeDatabase", on_close_database),
        ("getDatabaseStatus", on_get_database_status),
        ("getConnections", on_get_connections),
        ("getFolders", on_get_folders),
    ];

    for (name, handler) in bindings {
        let cname = CString::new(*name).unwrap();
        unsafe {
            webui_sys::webui_bind(window, cname.as_ptr(), Some(*handler));
        }
        info!("Bound JS handler: {name}");
    }
}

// -- Helper: get ConfigManager lock --

fn with_config<F, R>(f: F) -> Option<R>
where
    F: FnOnce(&mut ConfigManager) -> R,
{
    STATE.get().and_then(|arc| {
        let mut guard = arc.lock().ok()?;
        Some(f(&mut guard))
    })
}

// -- Helper: extract string param from event --

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

// -- Cross-platform file dialogs --
// Windows/macOS: rfd (native dialogs). Linux: zenity/kdialog fallback.

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

#[cfg(target_os = "linux")]
fn pick_save_file(title: &str, default_name: &str) -> Option<PathBuf> {
    // Try zenity first, then kdialog
    let output = std::process::Command::new("zenity")
        .args([
            "--file-selection",
            "--save",
            "--confirm-overwrite",
            &format!("--title={title}"),
            &format!("--filename={default_name}"),
        ])
        .output()
        .ok()?;
    if output.status.success() {
        let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
        if !path.is_empty() {
            return Some(PathBuf::from(path));
        }
    }
    let output = std::process::Command::new("kdialog")
        .args([
            "--getsavefilename",
            "~",
            "*.db *.sqlite *.sqlite3|Database files",
        ])
        .output()
        .ok()?;
    if output.status.success() {
        let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
        if !path.is_empty() {
            return Some(PathBuf::from(path));
        }
    }
    None
}

#[cfg(target_os = "linux")]
fn pick_open_file(title: &str) -> Option<PathBuf> {
    let output = std::process::Command::new("zenity")
        .args([
            "--file-selection",
            &format!("--title={title}"),
            "--file-filter=Database files | *.db *.sqlite *.sqlite3",
            "--file-filter=All files | *",
        ])
        .output()
        .ok()?;
    if output.status.success() {
        let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
        if !path.is_empty() {
            return Some(PathBuf::from(path));
        }
    }
    let output = std::process::Command::new("kdialog")
        .args([
            "--getopenfilename",
            "~",
            "*.db *.sqlite *.sqlite3|Database files",
        ])
        .output()
        .ok()?;
    if output.status.success() {
        let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
        if !path.is_empty() {
            return Some(PathBuf::from(path));
        }
    }
    None
}

// -- Database handlers --

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

// -- Data query handlers --

unsafe extern "C" fn on_get_connections(e: *mut webui_sys::webui_event_t) {
    let json = with_config(|cm| cm.get_connections_json()).unwrap_or_else(|| "[]".to_string());
    unsafe { return_string(e, &json) };
}

unsafe extern "C" fn on_get_folders(e: *mut webui_sys::webui_event_t) {
    let json = with_config(|cm| cm.get_folders_json()).unwrap_or_else(|| "[]".to_string());
    unsafe { return_string(e, &json) };
}
