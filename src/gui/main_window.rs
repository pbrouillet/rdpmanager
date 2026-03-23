//! Main WebUI window and application lifecycle.
//!
//! Creates a WebUI window, serves the React UI (embedded or from disk),
//! and blocks until the window is closed.
//!
//! Equivalent to: `src/gui/main_window.cpp` / `main_window.hpp`

use log::{error, info, warn};
use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::path::PathBuf;

use crate::embedded_ui;

pub struct MainWindow {
    window: usize,
}

impl MainWindow {
    pub fn new() -> Self {
        let window = unsafe { webui_sys::webui_new_window() };
        Self { window }
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
        let ok = unsafe { webui_sys::webui_show(self.window, content.as_ptr()) };
        if !ok {
            warn!("webui_show returned false — trying browser fallback");
            // Try any browser as fallback
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
