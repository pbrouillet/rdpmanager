//! Main WebUI window and application lifecycle.
//!
//! Creates a WebUI window, serves the React UI (embedded or from disk),
//! and blocks until the window is closed.
//!
//! Equivalent to: `src/gui/main_window.cpp` / `main_window.hpp`

use log::{error, info, warn};
use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use crate::config_manager::ConfigManager;
use crate::dialog_manager::DialogManager;
use crate::embedded_ui;
use crate::gui::aad_auth_handler::AADAuthHandler;
use crate::js_handlers;
use crate::session_manager::SessionManager;

pub struct MainWindow {
    window: usize,
    debug_port: std::cell::Cell<u16>,
    config_manager: Arc<Mutex<ConfigManager>>,
    session_manager: Arc<Mutex<SessionManager>>,
    aad_handler: Arc<AADAuthHandler>,
    dialog_manager: Arc<DialogManager>,
}

impl MainWindow {
    pub fn new() -> Self {
        let window = unsafe { webui_sys::webui_new_window() };
        let config_manager = Arc::new(Mutex::new(ConfigManager::new()));
        let dialog_manager = Arc::new(DialogManager::new(window));
        let aad_handler = Arc::new(AADAuthHandler::new());
        aad_handler.set_main_window(window);
        aad_handler.register_global();
        let session_manager = Arc::new(Mutex::new(SessionManager::new(
            window,
            Arc::clone(&dialog_manager),
            Some(Arc::clone(&aad_handler)),
            Some(Arc::clone(&config_manager)),
        )));
        Self {
            window,
            debug_port: std::cell::Cell::new(0),
            config_manager,
            session_manager,
            aad_handler,
            dialog_manager,
        }
    }

    /// Set Chrome DevTools remote debugging port (0 = disabled).
    pub fn set_debug_port(&self, port: u16) {
        self.debug_port.set(port);
        if port > 0 {
            let debug_profile = "/tmp/webui-rdp-debug-profile";
            let params = format!(
                "--no-first-run --disable-extensions --disable-background-mode \
                 --disable-sync --allow-insecure-localhost \
                 --user-data-dir={debug_profile} \
                 --remote-debugging-port={port}"
            );
            let params_c = std::ffi::CString::new(params).unwrap();
            unsafe {
                webui_sys::webui_set_custom_parameters(self.window, params_c.into_raw());
            }
            info!("Chrome DevTools debugging enabled on port {port}");
            info!("Connect at: edge://inspect or http://localhost:{port}");
        }
    }

    /// Initialize the window: configure size, set up UI file serving.
    pub fn initialize(&self) -> bool {
        unsafe {
            webui_sys::webui_set_size(self.window, 1200, 800);
        }

        // Register an all-events handler so we can detect window disconnect
        // and call webui_exit() to unblock webui_wait().
        let element = CString::new("").unwrap();
        unsafe {
            webui_sys::webui_bind(self.window, element.as_ptr(), Some(on_event));
        }

        // Register JS→Rust bindings for database, connections, folders, sessions, etc.
        js_handlers::bind_all(
            self.window,
            Arc::clone(&self.config_manager),
            Arc::clone(&self.session_manager),
            Some(Arc::clone(&self.aad_handler)),
            Arc::clone(&self.dialog_manager),
        );

        if embedded_ui::has_files() {
            info!("Using embedded UI (VFS mode — assets compiled into binary)");
            unsafe {
                // Disable cookies: WebUI's cookie check rejects CSS/JS requests
                // that arrive before webui.js has set the auth cookie.
                // use_cookies = 4 in webui_config enum
                webui_sys::webui_set_config(4, false);
                webui_sys::webui_set_file_handler(self.window, Some(vfs_handler));
            }
        } else {
            info!("No embedded UI — searching for UI files on disk");
            if let Some(ui_path) = find_ui_path() {
                let path_cstr = CString::new(ui_path.to_str().unwrap()).unwrap();
                let ok =
                    unsafe { webui_sys::webui_set_root_folder(self.window, path_cstr.as_ptr()) };
                if !ok {
                    error!("Failed to set root folder: {}", ui_path.display());
                    return false;
                }
                info!("Serving UI from disk: {}", ui_path.display());
            } else {
                error!(
                    "No UI files found! Build the React UI first (npm run build in src/ui-react/)"
                );
                return false;
            }
        }

        true
    }

    /// Show the window and return whether it opened successfully.
    pub fn show(&self) -> bool {
        let content = CString::new("index.html").unwrap();

        // Use browser mode when debugging (WebView doesn't support DevTools)
        if self.debug_port.get() > 0 {
            info!("Using browser mode for debugging (Edge)");
            // Edge = 7, ChromiumBased = 3
            let ok = unsafe { webui_sys::webui_show_browser(self.window, content.as_ptr(), 7) };
            if ok {
                return true;
            }
            warn!("Edge not available, trying any Chromium-based browser");
            let ok = unsafe { webui_sys::webui_show_browser(self.window, content.as_ptr(), 3) };
            if ok {
                return true;
            }
            error!("Failed to open UI in any Chromium browser for debugging");
            return false;
        }

        let ok = unsafe { webui_sys::webui_show(self.window, content.as_ptr()) };
        if !ok {
            warn!("webui_show returned false — trying browser fallback");
            // Try any browser as fallback (AnyBrowser = 1)
            let ok = unsafe { webui_sys::webui_show_browser(self.window, content.as_ptr(), 1) };
            if !ok {
                error!("Failed to open UI in any browser or WebView");
                return false;
            }
        }
        true
    }

    /// Block until the window is closed.
    pub fn wait() {
        unsafe {
            webui_sys::webui_set_timeout(0); // wait forever
            webui_sys::webui_wait();
            webui_sys::webui_clean();
        }
    }
}

/// WebUI all-events handler — calls webui_exit() on disconnect so webui_wait() returns.
unsafe extern "C" fn on_event(e: *mut webui_sys::webui_event_t) {
    let event = unsafe { &*e };
    // WEBUI_EVENT_DISCONNECTED = 0
    if event.event_type == 0 {
        info!("Window disconnected — signalling exit");
        unsafe {
            webui_sys::webui_exit();
        }
    }
}

/// WebUI VFS file handler callback — serves embedded UI assets.
///
/// Returns a full HTTP response (headers + body) allocated with `webui_malloc`,
/// or null to let WebUI handle the request.
unsafe extern "C" fn vfs_handler(filename: *const c_char, length: *mut c_int) -> *const c_void {
    let path_cstr = unsafe { CStr::from_ptr(filename) };
    let path = path_cstr.to_str().unwrap_or("");

    if let Some(file) = embedded_ui::lookup(path) {
        // Build HTTP response: headers + body
        let header = format!(
            "HTTP/1.1 200 OK\r\nContent-Type: {}\r\nContent-Length: {}\r\nCache-Control: no-cache\r\n\r\n",
            file.mime_type,
            file.data.len()
        );
        let total_len = header.len() + file.data.len();

        unsafe {
            let buf = webui_sys::webui_malloc(total_len) as *mut u8;
            if buf.is_null() {
                return std::ptr::null();
            }
            std::ptr::copy_nonoverlapping(header.as_ptr(), buf, header.len());
            std::ptr::copy_nonoverlapping(
                file.data.as_ptr(),
                buf.add(header.len()),
                file.data.len(),
            );
            *length = total_len as c_int;
            buf as *const c_void
        }
    } else {
        std::ptr::null()
    }
}

/// Search for UI files on disk (mirrors C++ path_utils::find_ui_path).
fn find_ui_path() -> Option<PathBuf> {
    let candidates = [
        "ui-dist",
        "src/ui-react/dist",
        "../src/ui-react/dist",
        "src/ui",
        "../src/ui",
    ];

    // Also try relative to executable
    let exe_dir = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()));

    for candidate in &candidates {
        let path = PathBuf::from(candidate);
        if path.join("index.html").exists() {
            return Some(path);
        }
    }

    if let Some(ref dir) = exe_dir {
        let path = dir.join("ui");
        if path.join("index.html").exists() {
            return Some(path);
        }
    }

    None
}
