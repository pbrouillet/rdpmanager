//! Platform-specific window embedding for FreeRDP sessions.
//!
//! Windows: FreeRDP creates a standalone top-level window. After connection,
//!   we find it by matching GWLP_USERDATA (== rdpContext pointer) and reparent
//!   it into the WebUI browser window via SetParent + WS_CHILD style change.
//! Linux: FreeRDP_ParentWindowId + XReparentWindow (future)
//! macOS: NSView embedding or separate window fallback (future)

use log::info;
#[cfg(target_os = "windows")]
use log::warn;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[cfg(target_os = "windows")]
mod win32 {
    use std::ffi::c_int;
    #[allow(clippy::upper_case_acronyms)]
    type HWND = *mut std::ffi::c_void;
    #[allow(clippy::upper_case_acronyms)]
    type BOOL = c_int;
    #[allow(clippy::upper_case_acronyms)]
    type UINT = u32;

    pub const SW_SHOW: c_int = 5;
    pub const SW_HIDE: c_int = 0;
    pub const SWP_NOZORDER: UINT = 0x0004;
    pub const GWL_STYLE: c_int = -16;
    pub const GWLP_USERDATA: c_int = -21;
    pub const WS_CHILD: isize = 0x40000000;
    pub const WS_VISIBLE: isize = 0x10000000;

    extern "system" {
        pub fn ShowWindow(hwnd: HWND, cmd: c_int) -> BOOL;
        pub fn SetWindowPos(
            hwnd: HWND,
            after: HWND,
            x: c_int,
            y: c_int,
            cx: c_int,
            cy: c_int,
            flags: UINT,
        ) -> BOOL;
        pub fn SetParent(child: HWND, parent: HWND) -> HWND;
        pub fn FindWindowExW(
            parent: HWND,
            child_after: HWND,
            class_name: *const u16,
            window_name: *const u16,
        ) -> HWND;
        pub fn GetWindowLongPtrW(hwnd: HWND, index: c_int) -> isize;
        pub fn SetWindowLongPtrW(hwnd: HWND, index: c_int, new_long: isize) -> isize;
    }
}

/// Bounding rectangle for the content area where an RDP session should render.
/// Coordinates are in pixels relative to the WebUI top-level window.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ContentRect {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

/// Find the FreeRDP top-level window (class "wfreerdp") whose GWLP_USERDATA
/// matches the given context address. FreeRDP's `wf_post_connect` sets
/// GWLP_USERDATA to the wfContext pointer, which shares the same address as
/// rdpContext due to struct extension.
#[cfg(target_os = "windows")]
pub fn find_freerdp_window(context_addr: usize) -> Option<usize> {
    let class_name: Vec<u16> = "wfreerdp\0".encode_utf16().collect();
    let mut hwnd = unsafe {
        win32::FindWindowExW(
            std::ptr::null_mut(), // search top-level windows
            std::ptr::null_mut(),
            class_name.as_ptr(),
            std::ptr::null(),
        )
    };
    while !hwnd.is_null() {
        let user_data = unsafe { win32::GetWindowLongPtrW(hwnd, win32::GWLP_USERDATA) };
        if user_data as usize == context_addr {
            return Some(hwnd as usize);
        }
        // Advance to the next "wfreerdp" top-level window
        hwnd = unsafe {
            win32::FindWindowExW(
                std::ptr::null_mut(),
                hwnd,
                class_name.as_ptr(),
                std::ptr::null(),
            )
        };
    }
    None
}

/// Immediately reparent a FreeRDP HWND into the WebUI container.
/// Called from the background thread as soon as the HWND is discovered.
/// Hides the window, strips decorations, and reparents — but does NOT
/// position or show it (that happens later in show_session/reposition_session).
#[cfg(target_os = "windows")]
pub fn reparent_to_webui(hwnd: usize, parent_hwnd: usize) {
    unsafe {
        win32::ShowWindow(hwnd as *mut _, win32::SW_HIDE);
        win32::SetWindowLongPtrW(hwnd as *mut _, win32::GWL_STYLE, win32::WS_CHILD);
        win32::SetParent(hwnd as *mut _, parent_hwnd as *mut _);
    }
    info!(
        "reparent_to_webui: hwnd=0x{:x} → parent=0x{:x}",
        hwnd, parent_hwnd
    );
}

/// Manages native window containers for embedded RDP sessions.
///
/// On Windows, reparents FreeRDP HWNDs into the WebUI browser window and
/// controls their visibility/position. On other platforms, falls back to
/// separate top-level windows (no-op).
pub struct EmbeddingHost {
    #[allow(dead_code)] // Used on Windows; reserved for Linux/macOS embedding
    webui_window: usize,
    /// Maps session_id → FreeRDP native window handle (HWND as usize on Windows).
    sessions: HashMap<String, usize>,
}

impl EmbeddingHost {
    pub fn new(webui_window: usize) -> Self {
        info!("EmbeddingHost created (webui_window={})", webui_window);
        Self {
            webui_window,
            sessions: HashMap::new(),
        }
    }

    /// Whether native embedding is available on this platform.
    pub fn is_embedding_supported(&self) -> bool {
        cfg!(target_os = "windows")
    }

    /// Get the native parent window handle for FreeRDP embedding.
    /// On Windows, returns the WebUI HWND via `webui_get_hwnd`.
    /// Returns None on unsupported platforms.
    pub fn get_parent_window_id(&self) -> Option<u64> {
        #[cfg(target_os = "windows")]
        {
            let hwnd = unsafe { webui_sys::webui_get_hwnd(self.webui_window) };
            if hwnd.is_null() {
                warn!("webui_get_hwnd returned null");
                None
            } else {
                Some(hwnd as u64)
            }
        }
        #[cfg(not(target_os = "windows"))]
        {
            // Linux/macOS: not yet implemented
            None
        }
    }

    /// Register a session's native window handle for embedding management.
    pub fn add_session(&mut self, session_id: &str, hwnd: usize) {
        info!("add_session {} (hwnd=0x{:x})", session_id, hwnd);
        self.sessions.insert(session_id.to_string(), hwnd);
    }

    /// Check if a session's window handle is already tracked.
    pub fn has_session(&self, session_id: &str) -> bool {
        self.sessions.contains_key(session_id)
    }

    /// Show a session's embedded window (already reparented by the background thread).
    pub fn show_session(&self, session_id: &str) {
        #[cfg(target_os = "windows")]
        {
            if let Some(&hwnd) = self.sessions.get(session_id) {
                info!("show_session {} (hwnd=0x{:x})", session_id, hwnd);
                unsafe {
                    win32::ShowWindow(hwnd as *mut _, win32::SW_SHOW);
                }
            } else {
                warn!("show_session: session {} not tracked", session_id);
            }
        }
        #[cfg(not(target_os = "windows"))]
        {
            info!("show_session {} (no-op on this platform)", session_id);
        }
    }

    /// Hide a session's embedded window.
    pub fn hide_session(&self, session_id: &str) {
        #[cfg(target_os = "windows")]
        {
            if let Some(&hwnd) = self.sessions.get(session_id) {
                info!("hide_session {} (hwnd=0x{:x})", session_id, hwnd);
                unsafe {
                    win32::ShowWindow(hwnd as *mut _, win32::SW_HIDE);
                }
            } else {
                warn!("hide_session: session {} not tracked", session_id);
            }
        }
        #[cfg(not(target_os = "windows"))]
        {
            info!("hide_session {} (no-op on this platform)", session_id);
        }
    }

    /// Reposition a session's embedded window to match the content area.
    pub fn reposition_session(&self, session_id: &str, rect: &ContentRect) {
        #[cfg(target_os = "windows")]
        {
            if let Some(&hwnd) = self.sessions.get(session_id) {
                info!(
                    "reposition_session {} to ({},{} {}x{})",
                    session_id, rect.x, rect.y, rect.width, rect.height
                );
                unsafe {
                    win32::SetWindowPos(
                        hwnd as *mut _,
                        std::ptr::null_mut(),
                        rect.x,
                        rect.y,
                        rect.width as i32,
                        rect.height as i32,
                        win32::SWP_NOZORDER,
                    );
                }
            } else {
                warn!("reposition_session: session {} not tracked", session_id);
            }
        }
        #[cfg(not(target_os = "windows"))]
        {
            let _ = rect;
            info!("reposition_session {} (no-op on this platform)", session_id);
        }
    }

    /// Remove a session's embedded window — detaches from parent and removes tracking.
    pub fn remove_session(&mut self, session_id: &str) {
        #[cfg(target_os = "windows")]
        {
            if let Some(hwnd) = self.sessions.remove(session_id) {
                info!("remove_session {} (hwnd=0x{:x})", session_id, hwnd);
                unsafe {
                    // Detach from parent — reparent to desktop
                    win32::SetParent(hwnd as *mut _, std::ptr::null_mut());
                }
            } else {
                warn!("remove_session: session {} not tracked", session_id);
            }
        }
        #[cfg(not(target_os = "windows"))]
        {
            self.sessions.remove(session_id);
            info!("remove_session {} (no-op on this platform)", session_id);
        }
    }
}
