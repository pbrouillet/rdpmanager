//! Platform-specific window embedding for FreeRDP sessions.
//!
//! Windows: Uses Win32 SetParent()/ShowWindow()/SetWindowPos() to reparent the
//!   FreeRDP HWND into the WebUI browser window. FreeRDP_ParentWindowId is set
//!   before launch so FreeRDP auto-configures EmbeddedWindow and Decorations.
//! Linux: FreeRDP_ParentWindowId + XReparentWindow (future)
//! macOS: NSView embedding or separate window fallback (future)

use log::{info, warn};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[cfg(target_os = "windows")]
mod win32 {
    use std::ffi::c_int;
    type HWND = *mut std::ffi::c_void;
    type BOOL = c_int;
    type UINT = u32;

    pub const SW_SHOW: c_int = 5;
    pub const SW_HIDE: c_int = 0;
    pub const SWP_NOZORDER: UINT = 0x0004;

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
    #[allow(dead_code)] // Will be called once wfContext HWND extraction is implemented
    pub fn add_session(&mut self, session_id: &str, hwnd: usize) {
        info!("add_session {} (hwnd=0x{:x})", session_id, hwnd);
        self.sessions.insert(session_id.to_string(), hwnd);
    }

    /// Show a session's embedded window and reparent it into the WebUI window.
    pub fn show_session(&self, session_id: &str) {
        #[cfg(target_os = "windows")]
        {
            if let Some(&hwnd) = self.sessions.get(session_id) {
                let parent = self.get_parent_window_id().unwrap_or(0) as usize;
                info!(
                    "show_session {} (hwnd=0x{:x}, parent=0x{:x})",
                    session_id, hwnd, parent
                );
                unsafe {
                    if parent != 0 {
                        win32::SetParent(hwnd as *mut _, parent as *mut _);
                    }
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
