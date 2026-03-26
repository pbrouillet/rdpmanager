//! FreeRDP session management and threading.
//!
//! Spawns FreeRDP sessions on background threads using the FreeRDP library API.
//! Each platform links its native FreeRDP client library:
//!   - Linux: xfreerdp-client3 (X11 rendering)
//!   - Windows: wfreerdp-client3 (Win32 GDI rendering)
//!   - macOS: MacFreeRDP-library (Cocoa/NSView rendering)
//!
//! Equivalent to: `src/rdp_launcher.cpp` / `rdp_launcher.hpp`

use crate::dialog_manager::DialogManager;
use crate::types::{
    AuthRequest, CertificateInfo, ConnectionProfile, RDPConnectionState,
};
use log::{info, warn};
use serde::Serialize;
use std::collections::HashMap;
use std::ffi::CStr;
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
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
    /// FreeRDP instance pointer (as usize) — used as key in the global session map.
    instance_ptr: usize,
    /// Native window handle (HWND on Windows) for the FreeRDP rendering window.
    /// Currently None — extracting the HWND from wfContext requires bindgen to
    /// expose the platform-specific struct fields in a future iteration.
    native_window: Option<u64>,
}

impl RDPSession {
    pub fn get_state(&self) -> RDPConnectionState {
        u8_to_state(self.state.load(Ordering::Relaxed))
    }

    /// Get the native window handle for this session's FreeRDP window.
    /// Returns None until wfContext HWND extraction is implemented.
    #[allow(dead_code)]
    pub fn get_native_window(&self) -> Option<u64> {
        self.native_window
    }

    pub fn info(&self) -> SessionInfo {
        SessionInfo {
            id: self.id.clone(),
            hostname: self.hostname.clone(),
            state: state_name(self.get_state()).to_string(),
        }
    }
}

// Platform client libraries provide RdpClientEntry:
// Linux: xfreerdp-client3, Windows: wfreerdp-client3, macOS: MacFreeRDP-library
extern "C" {
    fn RdpClientEntry(entry: *mut freerdp_sys::RDP_CLIENT_ENTRY_POINTS_V1) -> std::ffi::c_int;
}

// ── Global session map ──────────────────────────────────────────────────

/// Per-session state accessible from static FreeRDP callbacks.
struct SessionCallbackState {
    dialog_manager: Arc<DialogManager>,
    hostname: String,
}

/// Maps FreeRDP instance pointers to their callback state.
/// Used by the static `extern "C"` callback trampolines to find the
/// DialogManager for a given FreeRDP session.
static SESSION_MAP: OnceLock<Mutex<HashMap<usize, Arc<SessionCallbackState>>>> = OnceLock::new();

fn get_session_map() -> &'static Mutex<HashMap<usize, Arc<SessionCallbackState>>> {
    SESSION_MAP.get_or_init(|| Mutex::new(HashMap::new()))
}

// ── FreeRDP callback trampolines ────────────────────────────────────────

const VERIFY_CERT_FLAG_CHANGED: u32 = 0x40;

/// Convert a nullable C string pointer to a Rust String.
unsafe fn ptr_to_string(ptr: *const std::ffi::c_char) -> String {
    if ptr.is_null() {
        String::new()
    } else {
        CStr::from_ptr(ptr).to_string_lossy().into_owned()
    }
}

/// Allocate a C string via malloc (compatible with FreeRDP's `free()`).
unsafe fn c_strdup(s: &str) -> *mut std::ffi::c_char {
    let bytes = s.as_bytes();
    let len = bytes.len() + 1;
    let ptr = libc::malloc(len) as *mut u8;
    if !ptr.is_null() {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), ptr, bytes.len());
        *ptr.add(bytes.len()) = 0;
    }
    ptr as *mut std::ffi::c_char
}

/// FreeRDP certificate verification callback.
/// Looks up the session in the global map and delegates to DialogManager.
unsafe extern "C" fn verify_certificate_cb(
    instance: *mut freerdp_sys::freerdp,
    host: *const std::ffi::c_char,
    port: u16,
    common_name: *const std::ffi::c_char,
    subject: *const std::ffi::c_char,
    issuer: *const std::ffi::c_char,
    fingerprint: *const std::ffi::c_char,
    flags: u32,
) -> u32 {
    let state = {
        let map = get_session_map().lock().unwrap();
        map.get(&(instance as usize)).cloned()
    };

    let Some(state) = state else {
        warn!("verify_certificate_cb: no session found for instance");
        return 0;
    };

    let info = CertificateInfo {
        host: ptr_to_string(host),
        port,
        common_name: ptr_to_string(common_name),
        subject: ptr_to_string(subject),
        issuer: ptr_to_string(issuer),
        fingerprint: ptr_to_string(fingerprint),
        is_changed: flags & VERIFY_CERT_FLAG_CHANGED != 0,
        old_fingerprint: String::new(),
    };

    state.dialog_manager.handle_certificate_verify(&info) as u32
}

/// Shared implementation for Authenticate and GatewayAuthenticate callbacks.
unsafe fn handle_auth(
    instance: *mut freerdp_sys::freerdp,
    username: *mut *mut std::ffi::c_char,
    password: *mut *mut std::ffi::c_char,
    domain: *mut *mut std::ffi::c_char,
    is_gateway: bool,
) -> i32 {
    let state = {
        let map = get_session_map().lock().unwrap();
        map.get(&(instance as usize)).cloned()
    };

    let Some(state) = state else {
        warn!("authenticate_cb: no session found for instance");
        return 0;
    };

    let request = AuthRequest {
        hostname: state.hostname.clone(),
        is_gateway,
        current_username: if !username.is_null() && !(*username).is_null() {
            ptr_to_string(*username)
        } else {
            String::new()
        },
        current_domain: if !domain.is_null() && !(*domain).is_null() {
            ptr_to_string(*domain)
        } else {
            String::new()
        },
    };

    let response = state.dialog_manager.handle_authenticate(&request);

    if response.success {
        if !username.is_null() {
            if !(*username).is_null() {
                libc::free(*username as *mut std::ffi::c_void);
            }
            *username = c_strdup(&response.username);
        }
        if !password.is_null() {
            if !(*password).is_null() {
                libc::free(*password as *mut std::ffi::c_void);
            }
            *password = c_strdup(&response.password);
        }
        if !domain.is_null() {
            if !(*domain).is_null() {
                libc::free(*domain as *mut std::ffi::c_void);
            }
            *domain = c_strdup(&response.domain);
        }
        1 // TRUE
    } else {
        0 // FALSE
    }
}

/// FreeRDP authentication callback (NLA/TLS/RDP credentials).
unsafe extern "C" fn authenticate_cb(
    instance: *mut freerdp_sys::freerdp,
    username: *mut *mut std::ffi::c_char,
    password: *mut *mut std::ffi::c_char,
    domain: *mut *mut std::ffi::c_char,
) -> i32 {
    handle_auth(instance, username, password, domain, false)
}

/// FreeRDP gateway authentication callback (same signature as Authenticate).
unsafe extern "C" fn gateway_authenticate_cb(
    instance: *mut freerdp_sys::freerdp,
    username: *mut *mut std::ffi::c_char,
    password: *mut *mut std::ffi::c_char,
    domain: *mut *mut std::ffi::c_char,
) -> i32 {
    handle_auth(instance, username, password, domain, true)
}

// ── RDPLauncher ─────────────────────────────────────────────────────────

/// Manages FreeRDP sessions.
pub struct RDPLauncher {
    sessions: HashMap<String, Arc<Mutex<RDPSession>>>,
    dialog_manager: Option<Arc<DialogManager>>,
}

impl RDPLauncher {
    pub fn new() -> Self {
        Self {
            sessions: HashMap::new(),
            dialog_manager: None,
        }
    }

    /// Set the dialog manager for certificate/auth callbacks.
    pub fn set_dialog_manager(&mut self, dm: Arc<DialogManager>) {
        self.dialog_manager = Some(dm);
    }

    /// Launch a new RDP session. Returns session ID on success.
    /// If `parent_window_id` is provided, FreeRDP will embed its window as a
    /// child of that native window handle (HWND on Windows).
    pub fn launch(
        &mut self,
        profile: &ConnectionProfile,
        parent_window_id: Option<u64>,
    ) -> Result<String, String> {
        let session_id = uuid::Uuid::new_v4().to_string();

        info!(
            "Launching RDP session {} to {}:{} (parent_window_id={:?})",
            session_id, profile.hostname, profile.port, parent_window_id
        );

        let session = self.launch_freerdp(&session_id, profile, parent_window_id)?;
        self.sessions
            .insert(session_id.clone(), Arc::new(Mutex::new(session)));
        Ok(session_id)
    }

    /// Launch using FreeRDP library API (platform client provides native window).
    fn launch_freerdp(
        &self,
        session_id: &str,
        profile: &ConnectionProfile,
        parent_window_id: Option<u64>,
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

            self.apply_settings(settings, profile, parent_window_id);

            // Install FreeRDP callbacks for certificate verification and authentication
            let instance = (*context).instance;
            let instance_addr = if !instance.is_null() {
                if let Some(dm) = &self.dialog_manager {
                    let cb_state = Arc::new(SessionCallbackState {
                        dialog_manager: Arc::clone(dm),
                        hostname: profile.hostname.clone(),
                    });
                    get_session_map()
                        .lock()
                        .unwrap()
                        .insert(instance as usize, cb_state);

                    (*instance).VerifyCertificateEx = Some(verify_certificate_cb);
                    (*instance).Authenticate = Some(authenticate_cb);
                    (*instance).GatewayAuthenticate = Some(gateway_authenticate_cb);
                    // GetAccessToken left unset — AAD handler will be implemented separately
                    info!("Installed FreeRDP callbacks for session {}", session_id);
                }
                instance as usize
            } else {
                0
            };

            // Start FreeRDP client (spawns internal thread)
            let start_rc = freerdp_sys::freerdp_client_start(context);
            if start_rc != 0 {
                // Clean up session map on start failure
                if instance_addr != 0 {
                    get_session_map().lock().unwrap().remove(&instance_addr);
                }
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

                // Remove from global session map before freeing context
                if instance_addr != 0 {
                    get_session_map().lock().unwrap().remove(&instance_addr);
                }

                freerdp_sys::freerdp_client_stop(ctx);
                freerdp_sys::freerdp_client_context_free(ctx);
                info!("Session {} cleaned up", sid);
            });

            // TODO: Extract HWND from wfContext after freerdp_client_start().
            // Requires bindgen to expose wfContext struct fields.
            let native_window: Option<u64> = None;

            Ok(RDPSession {
                id: session_id.to_string(),
                hostname: profile.hostname.clone(),
                state,
                thread: Some(thread),
                context_ptr: context_addr,
                instance_ptr: instance_addr,
                native_window,
            })
        }
    }

    /// Apply ConnectionProfile settings to FreeRDP rdpSettings.
    unsafe fn apply_settings(
        &self,
        settings: *mut freerdp_sys::rdpSettings,
        profile: &ConnectionProfile,
        parent_window_id: Option<u64>,
    ) {
        use std::ffi::CString;

        macro_rules! set_str {
            ($key:ident, $val:expr) => {
                if let Ok(cs) = CString::new($val.as_str()) {
                    freerdp_sys::freerdp_settings_set_string(
                        settings,
                        freerdp_sys::$key as _,
                        cs.as_ptr(),
                    );
                }
            };
        }
        macro_rules! set_bool {
            ($key:ident, $val:expr) => {
                freerdp_sys::freerdp_settings_set_bool(
                    settings,
                    freerdp_sys::$key as _,
                    $val as i32,
                );
            };
        }
        macro_rules! set_u32 {
            ($key:ident, $val:expr) => {
                freerdp_sys::freerdp_settings_set_uint32(
                    settings,
                    freerdp_sys::$key as _,
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

        // When embedding, set ParentWindowId — FreeRDP auto-sets
        // EmbeddedWindow=true and Decorations=false.
        if let Some(parent_id) = parent_window_id {
            info!("Setting FreeRDP_ParentWindowId=0x{:x}", parent_id);
            freerdp_sys::freerdp_settings_set_uint64(
                settings,
                freerdp_sys::FreeRDP_ParentWindowId as _,
                parent_id,
            );
        } else {
            set_bool!(FreeRDP_Decorations, true);
        }

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

            // Remove from global session map (callback trampolines won't find it anymore)
            if session.instance_ptr != 0 {
                get_session_map()
                    .lock()
                    .unwrap()
                    .remove(&session.instance_ptr);
            }

            if session.context_ptr != 0 {
                unsafe {
                    let ctx = session.context_ptr as *mut freerdp_sys::rdpContext;
                    freerdp_sys::freerdp_client_stop(ctx);
                }
                // The background thread will clean up the context
                session.context_ptr = 0;
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

    /// Send a dynamic resolution update to an active session.
    pub fn send_resize(&self, session_id: &str, width: u32, height: u32) -> bool {
        if let Some(session_arc) = self.sessions.get(session_id) {
            let session = session_arc.lock().unwrap();
            if !matches!(session.get_state(), RDPConnectionState::Connected) {
                warn!("send_resize: session {} not connected", session_id);
                return false;
            }
            let ctx_ptr = session.context_ptr;
            if ctx_ptr == 0 {
                warn!("send_resize: session {} has null context", session_id);
                return false;
            }
            let w = width.clamp(200, 8192);
            let h = height.clamp(200, 8192);

            unsafe {
                let ctx = ctx_ptr as *mut freerdp_sys::rdpContext;
                let mut monitor = freerdp_sys::MONITOR_DEF {
                    left: 0,
                    top: 0,
                    right: w as i32 - 1,
                    bottom: h as i32 - 1,
                    flags: 1, // DISPLAY_CONTROL_MONITOR_PRIMARY
                };
                let ok = freerdp_sys::freerdp_display_send_monitor_layout(
                    ctx,
                    1,
                    &mut monitor as *mut _,
                );
                if ok != 0 {
                    info!("Sent display resize {}x{} to session {}", w, h, session_id);
                } else {
                    warn!("Failed to send display resize to session {}", session_id);
                }
                ok != 0
            }
        } else {
            warn!("send_resize: session {} not found", session_id);
            false
        }
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
                    // Ensure session is removed from global callback map
                    if session.instance_ptr != 0 {
                        get_session_map().lock().unwrap().remove(&session.instance_ptr);
                    }
                    if let Some(handle) = session.thread.take() {
                        drop(session);
                        let _: Result<(), _> = handle.join();
                    }
                }
            }
        }
    }
}
