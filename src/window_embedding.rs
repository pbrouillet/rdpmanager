//! Platform-specific window embedding for FreeRDP sessions.
//!
//! Phase 1: Stub implementation — FreeRDP creates separate top-level windows.
//! Phase 2: Native window reparenting:
//!   - Linux: FreeRDP_ParentWindowId + XReparentWindow (native FreeRDP support)
//!   - Windows: Win32 SetParent() on FreeRDP HWND
//!   - macOS: NSView embedding or separate window fallback

use log::info;
use serde::{Deserialize, Serialize};

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
/// Phase 1: No-op implementation. FreeRDP creates its own windows.
/// Phase 2: Creates child containers within the WebUI window and reparents
/// FreeRDP windows into them.
pub struct EmbeddingHost {
    #[allow(dead_code)]
    webui_window: usize,
}

impl EmbeddingHost {
    pub fn new(webui_window: usize) -> Self {
        info!("EmbeddingHost created (Phase 1: separate windows)");
        Self { webui_window }
    }

    /// Whether native embedding is available on this platform.
    pub fn is_embedding_supported(&self) -> bool {
        // Phase 1: no embedding — FreeRDP uses separate windows
        false
    }

    /// Get the native parent window handle for FreeRDP embedding.
    /// Returns None in Phase 1 (separate windows).
    #[allow(dead_code)]
    pub fn get_parent_window_id(&self) -> Option<u64> {
        // Phase 2 (Linux): get X11 Window ID from WebUI's GTK window
        // Phase 2 (Windows): get HWND from WebUI
        None
    }

    /// Show a session's embedded window.
    pub fn show_session(&self, session_id: &str) {
        // Phase 1: no-op (separate windows are always visible)
        info!("show_session {} (no-op in Phase 1)", session_id);
    }

    /// Hide a session's embedded window.
    pub fn hide_session(&self, session_id: &str) {
        // Phase 1: no-op
        info!("hide_session {} (no-op in Phase 1)", session_id);
    }

    /// Reposition a session's embedded window to match the content area.
    pub fn reposition_session(&self, session_id: &str, rect: &ContentRect) {
        // Phase 1: no-op
        let _ = rect;
        info!("reposition_session {} (no-op in Phase 1)", session_id);
    }

    /// Remove a session's embedded window container.
    pub fn remove_session(&self, session_id: &str) {
        // Phase 1: no-op
        info!("remove_session {} (no-op in Phase 1)", session_id);
    }
}
