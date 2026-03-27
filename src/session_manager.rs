//! Session + embedding coordinator.
//!
//! Manages active RDP sessions and coordinates between the RDP launcher,
//! the window embedding layer, and the React UI via JS bindings.

use crate::config_manager::ConfigManager;
use crate::dialog_manager::DialogManager;
use crate::gui::aad_auth_handler::AADAuthHandler;
use crate::rdp_launcher::{RDPLauncher, SessionInfo};
use crate::types::ConnectionProfile;
use crate::window_embedding::{ContentRect, EmbeddingHost};
use log::{info, warn};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};

/// Coordinates RDP sessions with window embedding and the UI.
pub struct SessionManager {
    launcher: RDPLauncher,
    embedding: EmbeddingHost,
    /// Maps session_id → content rect (last known position for the tab).
    rects: HashMap<String, ContentRect>,
    /// Currently visible session tab (None = home tab).
    active_session: Option<String>,
    /// WebUI window handle (for native HWND/XWindow access).
    webui_window: usize,
}

impl SessionManager {
    pub fn new(
        webui_window: usize,
        dialog_manager: Arc<DialogManager>,
        aad_handler: Option<Arc<AADAuthHandler>>,
        config_manager: Option<Arc<Mutex<ConfigManager>>>,
    ) -> Self {
        let mut launcher = RDPLauncher::new();
        launcher.set_dialog_manager(dialog_manager);
        if let Some(handler) = aad_handler {
            launcher.set_aad_handler(handler);
        }
        if let Some(cm) = config_manager {
            launcher.set_config_manager(cm);
        }
        Self {
            launcher,
            embedding: EmbeddingHost::new(webui_window),
            rects: HashMap::new(),
            active_session: None,
            webui_window,
        }
    }

    /// Connect to an RDP server. Returns session ID on success.
    /// If embedding is supported, passes the WebUI parent window ID to FreeRDP
    /// so the RDP window is created as a child of the browser window.
    pub fn connect(
        &mut self,
        profile: &ConnectionProfile,
        rect: ContentRect,
    ) -> Result<String, String> {
        let parent_window_id = self.embedding.get_parent_window_id();
        let session_id = self.launcher.launch(profile, parent_window_id)?;
        self.rects.insert(session_id.clone(), rect);
        self.active_session = Some(session_id.clone());

        info!("Session {} active — switching to RDP tab", session_id);
        Ok(session_id)
    }

    /// Switch to an RDP session tab.
    pub fn switch_tab(&mut self, session_id: &str, rect: ContentRect) -> bool {
        if self.launcher.get_session(session_id).is_none() {
            warn!("switch_tab: session {} not found", session_id);
            return false;
        }

        // Lazy-register: if the session's HWND has been captured by the
        // background thread but isn't yet tracked in the embedding host,
        // register it now before trying to show/reposition.
        if !self.embedding.has_session(session_id) {
            let hwnd = self.launcher.get_native_window(session_id);
            if hwnd != 0 {
                self.embedding.add_session(session_id, hwnd as usize);
            }
        }

        // Hide all other sessions
        let other_ids: Vec<String> = self
            .rects
            .keys()
            .filter(|id| id.as_str() != session_id)
            .cloned()
            .collect();
        for id in &other_ids {
            self.embedding.hide_session(id);
        }

        // Show and reposition the target session
        self.rects.insert(session_id.to_string(), rect.clone());
        self.embedding.show_session(session_id);
        self.embedding.reposition_session(session_id, &rect);
        self.active_session = Some(session_id.to_string());
        true
    }

    /// Switch to the home tab (hide all RDP sessions).
    pub fn show_home(&mut self) {
        for id in self.rects.keys() {
            self.embedding.hide_session(id);
        }
        self.active_session = None;
    }

    /// Resize an active session's display area.
    pub fn resize_session(&mut self, session_id: &str, rect: ContentRect) {
        self.embedding.reposition_session(session_id, &rect);
        self.launcher
            .send_resize(session_id, rect.width, rect.height);
        self.rects.insert(session_id.to_string(), rect);
    }

    /// Disconnect a session and clean up.
    pub fn disconnect(&mut self, session_id: &str) -> bool {
        self.embedding.remove_session(session_id);
        self.rects.remove(session_id);

        if self.active_session.as_deref() == Some(session_id) {
            self.active_session = None;
        }

        self.launcher.disconnect(session_id)
    }

    /// Get info about all active sessions (for tab bar rendering).
    pub fn get_sessions(&self) -> Vec<SessionInfo> {
        self.launcher.get_sessions()
    }

    /// Get the currently active session ID (None = home tab).
    pub fn active_session_id(&self) -> Option<&str> {
        self.active_session.as_deref()
    }

    /// Clean up finished sessions.
    pub fn cleanup(&mut self) {
        self.launcher.cleanup_finished();
        // Remove rects for sessions that no longer exist
        let active_ids: Vec<String> = self
            .launcher
            .get_sessions()
            .iter()
            .map(|s| s.id.clone())
            .collect();
        self.rects.retain(|id, _| active_ids.contains(id));
    }
}
