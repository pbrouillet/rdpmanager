//! Platform-specific window management for FreeRDP sessions.
//!
//! Currently operates in "popout" mode: FreeRDP creates a standalone top-level
//! window that the user interacts with directly. Session tracking is done by
//! session ID only — no native window handle manipulation.
//!
//! Future: embed the FreeRDP window into the WebUI container via
//! SetParent + WS_CHILD on Windows, XReparentWindow on Linux.

use log::info;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Bounding rectangle for the content area where an RDP session should render.
/// Coordinates are in pixels relative to the WebUI top-level window.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ContentRect {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

/// Manages FreeRDP session lifecycle tracking.
///
/// In popout mode, FreeRDP windows are standalone — this struct tracks
/// session IDs but does not manipulate native windows.
/// Future embedding support will add SetParent/reparenting on Windows.
pub struct EmbeddingHost {
    #[allow(dead_code)] // Reserved for future embedding support
    webui_window: usize,
    sessions: HashMap<String, ()>,
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
        false // Popout mode — embedding deferred
    }

    /// Register a session for tracking.
    pub fn add_session(&mut self, session_id: &str) {
        info!("add_session {}", session_id);
        self.sessions.insert(session_id.to_string(), ());
    }

    /// Check if a session is tracked.
    pub fn has_session(&self, session_id: &str) -> bool {
        self.sessions.contains_key(session_id)
    }

    /// Show a session's window. No-op in popout mode.
    pub fn show_session(&self, session_id: &str) {
        info!("show_session {} (popout mode — no-op)", session_id);
    }

    /// Hide a session's window. No-op in popout mode.
    pub fn hide_session(&self, session_id: &str) {
        info!("hide_session {} (popout mode — no-op)", session_id);
    }

    /// Reposition a session's window. No-op in popout mode.
    pub fn reposition_session(&self, session_id: &str, rect: &ContentRect) {
        let _ = rect;
        info!("reposition_session {} (popout mode — no-op)", session_id);
    }

    /// Remove a session from tracking.
    pub fn remove_session(&mut self, session_id: &str) {
        self.sessions.remove(session_id);
        info!("remove_session {} (popout mode)", session_id);
    }
}
