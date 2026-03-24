//! FreeRDP session management and threading.
//!
//! Spawns FreeRDP sessions on background threads using the FreeRDP library API.
//! On Linux, links to xfreerdp-client3 for X11 rendering.
//! On Windows/macOS, platform clients are not yet built — returns unsupported.
//!
//! Equivalent to: `src/rdp_launcher.cpp` / `rdp_launcher.hpp`

use crate::types::{ConnectionProfile, RDPConnectionState};
use log::{info, warn};
use serde::Serialize;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;

/// Session info exposed to the UI for tab display.
#[derive(Debug, Clone, Serialize)]
pub struct SessionInfo {
    pub id: String,
    pub hostname: String,
    pub state: String,
}

/// Atomic wrapper for RDPConnectionState.
fn state_to_u8(s: RDPConnectionState) -> u8 {
    match s {
        RDPConnectionState::Idle => 0,
        RDPConnectionState::Connecting => 1,
        RDPConnectionState::Connected => 2,
        RDPConnectionState::Disconnecting => 3,
        RDPConnectionState::Disconnected => 4,
        RDPConnectionState::Error => 5,
    }
}

fn u8_to_state(v: u8) -> RDPConnectionState {
    match v {
        0 => RDPConnectionState::Idle,
        1 => RDPConnectionState::Connecting,
        2 => RDPConnectionState::Connected,
        3 => RDPConnectionState::Disconnecting,
        4 => RDPConnectionState::Disconnected,
        5 => RDPConnectionState::Error,
        _ => RDPConnectionState::Error,
    }
}

fn state_name(s: RDPConnectionState) -> &'static str {
    match s {
        RDPConnectionState::Idle => "idle",
        RDPConnectionState::Connecting => "connecting",
        RDPConnectionState::Connected => "connected",
        RDPConnectionState::Disconnecting => "disconnecting",
        RDPConnectionState::Disconnected => "disconnected",
        RDPConnectionState::Error => "error",
    }
}

/// A running FreeRDP session.
pub struct RDPSession {
    pub id: String,
    pub hostname: String,
    state: Arc<AtomicU8>,
    thread: Option<JoinHandle<()>>,
    /// FreeRDP context pointer (as usize for Send safety). Only valid on Linux.
    #[allow(dead_code)]
    context_ptr: usize,
}

impl RDPSession {
    pub fn get_state(&self) -> RDPConnectionState {
        u8_to_state(self.state.load(Ordering::Relaxed))
    }

    pub fn info(&self) -> SessionInfo {
        SessionInfo {
            id: self.id.clone(),
            hostname: self.hostname.clone(),
            state: state_name(self.get_state()).to_string(),
        }
    }
}

// On Linux, link to xfreerdp-client3 which provides RdpClientEntry
#[cfg(target_os = "linux")]
extern "C" {
    fn RdpClientEntry(entry: *mut freerdp_sys::RDP_CLIENT_ENTRY_POINTS_V1) -> std::ffi::c_int;
}

/// Manages FreeRDP sessions.
pub struct RDPLauncher {
    sessions: HashMap<String, Arc<Mutex<RDPSession>>>,
}

impl RDPLauncher {
    pub fn new() -> Self {
        Self {
            sessions: HashMap::new(),
        }
    }

    /// Launch a new RDP session. Returns session ID on success.
    pub fn launch(&mut self, profile: &ConnectionProfile) -> Result<String, String> {
        let session_id = uuid::Uuid::new_v4().to_string();

        info!(
            "Launching RDP session {} to {}:{}",
            session_id, profile.hostname, profile.port
        );

        #[cfg(target_os = "linux")]
        {
            let session = self.launch_freerdp(&session_id, profile)?;
            self.sessions
                .insert(session_id.clone(), Arc::new(Mutex::new(session)));
            Ok(session_id)
        }

        #[cfg(not(target_os = "linux"))]
        {
            let _ = &session_id;
            Err("RDP sessions require the FreeRDP platform client. Currently only supported on Linux.".into())
        }
    }

    /// Launch using FreeRDP library API (Linux/X11 only).
    #[cfg(target_os = "linux")]
    fn launch_freerdp(
        &self,
        session_id: &str,
        profile: &ConnectionProfile,
    ) -> Result<RDPSession, String> {
        unsafe {
            // Initialize entry points
            let mut entry: freerdp_sys::RDP_CLIENT_ENTRY_POINTS_V1 = std::mem::zeroed();
            entry.Size = std::mem::size_of::<freerdp_sys::RDP_CLIENT_ENTRY_POINTS_V1>() as u32;

            let rc = RdpClientEntry(&mut entry);
            if rc != 0 {
                return Err(format!("RdpClientEntry failed: {}", rc));
            }

            // Create FreeRDP context
            let context = freerdp_sys::freerdp_client_context_new(
                &entry as *const freerdp_sys::RDP_CLIENT_ENTRY_POINTS_V1,
            );
            if context.is_null() {
                return Err("freerdp_client_context_new returned null".into());
            }

            // Apply connection settings
            let settings = (*context).settings;
            if settings.is_null() {
                freerdp_sys::freerdp_client_context_free(context);
                return Err("FreeRDP context has null settings".into());
            }

            self.apply_settings(settings, profile);

            // Start FreeRDP client (spawns internal thread)
            let start_rc = freerdp_sys::freerdp_client_start(context);
            if start_rc != 0 {
                freerdp_sys::freerdp_client_context_free(context);
                return Err(format!("freerdp_client_start failed: {}", start_rc));
            }

            let context_addr = context as usize;
            let state = Arc::new(AtomicU8::new(state_to_u8(RDPConnectionState::Connecting)));
            let state_clone = Arc::clone(&state);
            let sid = session_id.to_string();
            let host = profile.hostname.clone();

            // Background thread waits for the FreeRDP session to finish
            let thread = std::thread::spawn(move || {
                let ctx = context_addr as *mut freerdp_sys::rdpContext;
                info!("Session {} ({}) — waiting for FreeRDP thread", sid, host);

                state_clone.store(
                    state_to_u8(RDPConnectionState::Connected),
                    Ordering::Relaxed,
                );

                // Wait for internal FreeRDP thread to complete.
                // rdpClientContext extends rdpContext; the thread handle is in the
                // extended struct. We access it by casting.
                let cctx = ctx as *mut freerdp_sys::rdpClientContext;
                let thread_handle = (*cctx).thread;
                if !thread_handle.is_null() {
                    freerdp_sys::WaitForSingleObject(thread_handle, freerdp_sys::INFINITE);
                }

                info!("Session {} ({}) — FreeRDP thread exited", sid, host);
                state_clone.store(
                    state_to_u8(RDPConnectionState::Disconnected),
                    Ordering::Relaxed,
                );

                freerdp_sys::freerdp_client_stop(ctx);
                freerdp_sys::freerdp_client_context_free(ctx);
                info!("Session {} cleaned up", sid);
            });

            Ok(RDPSession {
                id: session_id.to_string(),
                hostname: profile.hostname.clone(),
                state,
                thread: Some(thread),
                context_ptr: context_addr,
            })
        }
    }

    /// Apply ConnectionProfile settings to FreeRDP rdpSettings.
    #[cfg(target_os = "linux")]
    unsafe fn apply_settings(
        &self,
        settings: *mut freerdp_sys::rdpSettings,
        profile: &ConnectionProfile,
    ) {
        use std::ffi::CString;

        macro_rules! set_str {
            ($key:ident, $val:expr) => {
                if let Ok(cs) = CString::new($val.as_str()) {
                    freerdp_sys::freerdp_settings_set_string(
                        settings,
                        freerdp_sys::$key as usize,
                        cs.as_ptr(),
                    );
                }
            };
        }
        macro_rules! set_bool {
            ($key:ident, $val:expr) => {
                freerdp_sys::freerdp_settings_set_bool(
                    settings,
                    freerdp_sys::$key as usize,
                    $val as i32,
                );
            };
        }
        macro_rules! set_u32 {
            ($key:ident, $val:expr) => {
                freerdp_sys::freerdp_settings_set_uint32(
                    settings,
                    freerdp_sys::$key as usize,
                    $val as u32,
                );
            };
        }

        // Connection
        set_str!(FreeRDP_ServerHostname, profile.hostname);
        set_u32!(FreeRDP_ServerPort, profile.port);

        if !profile.username.is_empty() {
            set_str!(FreeRDP_Username, profile.username);
        }
        if !profile.domain.is_empty() {
            set_str!(FreeRDP_Domain, profile.domain);
        }

        // Display
        set_u32!(FreeRDP_DesktopWidth, profile.width);
        set_u32!(FreeRDP_DesktopHeight, profile.height);
        set_bool!(FreeRDP_Fullscreen, profile.fullscreen);

        // Window title
        let title = if profile.remote_desktop_name.is_empty() {
            profile.hostname.clone()
        } else {
            profile.remote_desktop_name.clone()
        };
        set_str!(FreeRDP_WindowTitle, title);

        // No decorations when we'll embed (Phase 2); for now keep them
        set_bool!(FreeRDP_Decorations, true);

        // Dynamic resolution
        if profile.dynamic_resolution {
            set_bool!(FreeRDP_DynamicResolutionUpdate, true);
            set_bool!(FreeRDP_SupportDisplayControl, true);
        }

        // Features
        set_bool!(FreeRDP_RedirectClipboard, profile.clipboard);
        set_bool!(FreeRDP_DeviceRedirection, profile.home_drive);

        // Compression
        if profile.compression {
            set_bool!(FreeRDP_CompressionEnabled, true);
        }

        // Auto-reconnect
        if profile.auto_reconnect {
            set_bool!(FreeRDP_AutoReconnectionEnabled, true);
            set_u32!(
                FreeRDP_AutoReconnectMaxRetries,
                profile.auto_reconnect_max_retries
            );
        }

        // Certificate handling
        if profile.cert_tofu {
            set_bool!(FreeRDP_AutoAcceptCertificate, true);
        }

        // AAD / AVD
        if profile.enable_rds_aad_auth {
            set_bool!(FreeRDP_AadSecurity, true);
        }
        if !profile.gateway_hostname.is_empty() {
            set_str!(FreeRDP_GatewayHostname, profile.gateway_hostname);
            set_bool!(FreeRDP_GatewayEnabled, true);
        }
        if !profile.load_balance_info.is_empty() {
            set_str!(FreeRDP_LoadBalanceInfo, profile.load_balance_info);
        }

        // Graphics pipeline
        if profile.gfx_avc420 {
            set_bool!(FreeRDP_GfxAVC444, false);
            set_bool!(FreeRDP_GfxH264, true);
        }
    }

    /// Disconnect and remove a session.
    pub fn disconnect(&mut self, session_id: &str) -> bool {
        if let Some(session_arc) = self.sessions.remove(session_id) {
            let mut session = session_arc.lock().unwrap();
            info!(
                "Disconnecting session {} ({})",
                session_id, session.hostname
            );
            session.state.store(
                state_to_u8(RDPConnectionState::Disconnecting),
                Ordering::Relaxed,
            );

            #[cfg(target_os = "linux")]
            {
                if session.context_ptr != 0 {
                    unsafe {
                        let ctx = session.context_ptr as *mut freerdp_sys::rdpContext;
                        freerdp_sys::freerdp_client_stop(ctx);
                    }
                    // The background thread will clean up the context
                    session.context_ptr = 0;
                }
            }

            // Let the thread finish naturally
            if let Some(handle) = session.thread.take() {
                drop(session); // release lock before joining
                let _: Result<(), _> = handle.join();
            }
            true
        } else {
            warn!("Session {} not found", session_id);
            false
        }
    }

    /// Get info about all active sessions.
    pub fn get_sessions(&self) -> Vec<SessionInfo> {
        self.sessions
            .values()
            .filter_map(|s| s.lock().ok().map(|s| s.info()))
            .collect()
    }

    /// Get info about a specific session.
    pub fn get_session(&self, session_id: &str) -> Option<SessionInfo> {
        self.sessions
            .get(session_id)
            .and_then(|s| s.lock().ok().map(|s| s.info()))
    }

    /// Clean up completed sessions (already disconnected).
    pub fn cleanup_finished(&mut self) {
        let finished: Vec<String> = self
            .sessions
            .iter()
            .filter(|(_, s)| {
                s.lock()
                    .map(|s| {
                        matches!(
                            s.get_state(),
                            RDPConnectionState::Disconnected | RDPConnectionState::Error
                        )
                    })
                    .unwrap_or(false)
            })
            .map(|(id, _)| id.clone())
            .collect();

        for id in finished {
            if let Some(session_arc) = self.sessions.remove(&id) {
                if let Ok(mut session) = session_arc.lock() {
                    if let Some(handle) = session.thread.take() {
                        drop(session);
                        let _: Result<(), _> = handle.join();
                    }
                }
            }
        }
    }
}
