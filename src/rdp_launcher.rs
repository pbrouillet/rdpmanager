//! FreeRDP session management and threading.
//!
//! Spawns FreeRDP sessions on background threads using the FreeRDP library API.
//! Each platform links its native FreeRDP client library:
//!   - Linux: xfreerdp-client3 (X11 rendering)
//!   - Windows: wfreerdp-client3 (Win32 GDI rendering)
//!   - macOS: MacFreeRDP-library (Cocoa/NSView rendering)
//!
//! Equivalent to: `src/rdp_launcher.cpp` / `rdp_launcher.hpp`

use crate::config_manager::ConfigManager;
use crate::dialog_manager::DialogManager;
use crate::gui::aad_auth_handler::AADAuthHandler;
use crate::types::{
    AADAuthRequest, AADAuthType, AuthRequest, CertificateInfo, ConnectionProfile,
    RDPConnectionState,
};
use log::{error, info, warn};
use serde::Serialize;
use std::collections::HashMap;
use std::ffi::CStr;
use std::sync::atomic::{AtomicU64, AtomicU8, Ordering};
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
    /// Set asynchronously by the background thread after FreeRDP creates the window.
    native_window: Arc<AtomicU64>,
}

impl RDPSession {
    pub fn get_state(&self) -> RDPConnectionState {
        u8_to_state(self.state.load(Ordering::Relaxed))
    }

    /// Get the native window handle for this session's FreeRDP window.
    /// Returns 0 until the background thread discovers the HWND.
    pub fn get_native_window(&self) -> u64 {
        self.native_window.load(Ordering::Relaxed)
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
    aad_handler: Option<Arc<AADAuthHandler>>,
    config_manager: Option<Arc<Mutex<ConfigManager>>>,
    gateway_hostname: String,
    use_manual_code_flow: bool,
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
) -> freerdp_sys::BOOL {
    let state = {
        let map = get_session_map().lock().unwrap();
        map.get(&(instance as usize)).cloned()
    };

    let Some(state) = state else {
        warn!("authenticate_cb: no session found for instance");
        return 0 as freerdp_sys::BOOL;
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
        1 as freerdp_sys::BOOL // TRUE
    } else {
        0 as freerdp_sys::BOOL // FALSE
    }
}

/// FreeRDP authentication callback (NLA/TLS/RDP credentials).
unsafe extern "C" fn authenticate_cb(
    instance: *mut freerdp_sys::freerdp,
    username: *mut *mut std::ffi::c_char,
    password: *mut *mut std::ffi::c_char,
    domain: *mut *mut std::ffi::c_char,
) -> freerdp_sys::BOOL {
    handle_auth(instance, username, password, domain, false)
}

/// FreeRDP gateway authentication callback (same signature as Authenticate).
unsafe extern "C" fn gateway_authenticate_cb(
    instance: *mut freerdp_sys::freerdp,
    username: *mut *mut std::ffi::c_char,
    password: *mut *mut std::ffi::c_char,
    domain: *mut *mut std::ffi::c_char,
) -> freerdp_sys::BOOL {
    handle_auth(instance, username, password, domain, true)
}

/// FreeRDP AAD/Entra ID access token callback.
/// Called when FreeRDP needs an OAuth bearer token for AAD-joined hosts.
///
/// This is a variadic C callback (`pGetAccessToken`). We define it with only the
/// fixed parameters and use `transmute` when installing it, since stable Rust
/// cannot declare variadic fn items. The variadic args (scope, req_cnf) are
/// accessed via pointer arithmetic on the stack — only when `count >= 2` and
/// the platform ABI guarantees they follow the fixed params.
///
/// For ACCESS_TOKEN_TYPE_AAD (count=2): extra args are (scope: *const c_char, req_cnf: *const c_char)
/// For ACCESS_TOKEN_TYPE_AVD (count=0): no extra args
unsafe extern "C" fn get_access_token_cb(
    instance: *mut freerdp_sys::freerdp,
    token_type: freerdp_sys::AccessTokenType,
    token: *mut *mut std::os::raw::c_char,
    count: usize,
    // Variadic args follow on the stack. We declare the two known optional params
    // so the compiler reads them from the correct stack positions when present.
    scope_arg: *const std::os::raw::c_char,
    req_cnf_arg: *const std::os::raw::c_char,
) -> freerdp_sys::BOOL {
    if instance.is_null() || token.is_null() {
        return 0;
    }

    let state = {
        let map = get_session_map().lock().unwrap();
        map.get(&(instance as usize)).cloned()
    };

    let Some(state) = state else {
        warn!("get_access_token_cb: no session found for instance");
        return 0;
    };

    let Some(aad_handler) = &state.aad_handler else {
        warn!("get_access_token_cb: no AAD handler configured");
        return 0;
    };

    // Determine cache key and scope based on token type
    let (cache_kind, cache_hostname) = if token_type == freerdp_sys::ACCESS_TOKEN_TYPE_AVD {
        if !state.gateway_hostname.is_empty() {
            ("gateway".to_string(), state.gateway_hostname.clone())
        } else {
            ("machine".to_string(), state.hostname.clone())
        }
    } else {
        ("machine".to_string(), state.hostname.clone())
    };

    let cache_key = format!("aad:{}:{}", cache_kind, cache_hostname);

    // Extract scope from variadic args (AAD type) or use WVD default (AVD type)
    let scope = if token_type == freerdp_sys::ACCESS_TOKEN_TYPE_AAD && count >= 2 {
        ptr_to_string(scope_arg)
    } else {
        "https://www.wvd.microsoft.com/.default".to_string()
    };

    let req_cnf = if token_type == freerdp_sys::ACCESS_TOKEN_TYPE_AAD && count >= 2 {
        ptr_to_string(req_cnf_arg)
    } else {
        String::new()
    };

    // Check token cache first
    if let Some(ref cm_arc) = state.config_manager {
        if let Ok(cm) = cm_arc.lock() {
            if let Some(cached) = cm.get_cached_token(&cache_key, &scope) {
                info!(
                    "get_access_token_cb: using cached token for {} ({})",
                    cache_hostname, cache_kind
                );
                *token = c_strdup(&cached);
                return 1;
            }
        }
    }

    // Cache miss — build AAD auth request and invoke OAuth popup
    info!(
        "get_access_token_cb: requesting AAD auth for {} (type={})",
        cache_hostname, token_type
    );

    let auth_type = if token_type == freerdp_sys::ACCESS_TOKEN_TYPE_AVD {
        AADAuthType::Avd
    } else {
        AADAuthType::RdsAad
    };

    // Get the auth URL from FreeRDP's client context
    let context = (*instance).context;
    if context.is_null() {
        error!("get_access_token_cb: null context");
        return 0;
    }
    let cctx = context as *mut freerdp_sys::rdpClientContext;

    let aad_request_type = if token_type == freerdp_sys::ACCESS_TOKEN_TYPE_AVD {
        freerdp_sys::FREERDP_CLIENT_AAD_AVD_AUTH_REQUEST
    } else {
        freerdp_sys::FREERDP_CLIENT_AAD_AUTH_REQUEST
    };

    let auth_url_ptr = if token_type == freerdp_sys::ACCESS_TOKEN_TYPE_AAD {
        let scope_cstr = std::ffi::CString::new(scope.as_str()).unwrap_or_default();
        freerdp_sys::freerdp_client_get_aad_url(cctx, aad_request_type, scope_cstr.as_ptr())
    } else {
        freerdp_sys::freerdp_client_get_aad_url(cctx, aad_request_type)
    };

    let auth_url = if !auth_url_ptr.is_null() {
        let s = ptr_to_string(auth_url_ptr);
        libc::free(auth_url_ptr as *mut std::ffi::c_void);
        s
    } else {
        error!("get_access_token_cb: failed to generate auth URL");
        return 0;
    };

    let request = AADAuthRequest {
        auth_type,
        auth_url,
        scope: scope.clone(),
        req_cnf: req_cnf.clone(),
        use_ui_manual_code_flow: state.use_manual_code_flow,
        step_current: 1,
        step_total: if auth_type == AADAuthType::Avd { 2 } else { 1 },
        step_label: if auth_type == AADAuthType::Avd {
            "Gateway".to_string()
        } else {
            "Authentication".to_string()
        },
    };

    let response = aad_handler.handle_authenticate(&request);

    if !response.success || response.redirect_url.is_empty() {
        warn!(
            "get_access_token_cb: AAD auth failed for {}",
            cache_hostname
        );
        return 0;
    }

    info!("get_access_token_cb: AAD auth succeeded, exchanging code for token");

    // Extract authorization code from redirect URL
    let code = extract_code_from_url(&response.redirect_url);
    if code.is_empty() {
        error!("get_access_token_cb: failed to extract auth code from redirect URL");
        return 0;
    }

    // Build token request via FreeRDP helper
    let token_request_type = if token_type == freerdp_sys::ACCESS_TOKEN_TYPE_AVD {
        freerdp_sys::FREERDP_CLIENT_AAD_AVD_TOKEN_REQUEST
    } else {
        freerdp_sys::FREERDP_CLIENT_AAD_TOKEN_REQUEST
    };

    let code_cstr = std::ffi::CString::new(code.as_str()).unwrap_or_default();
    let token_request_ptr = if token_type == freerdp_sys::ACCESS_TOKEN_TYPE_AAD {
        let scope_cstr = std::ffi::CString::new(scope.as_str()).unwrap_or_default();
        let req_cnf_cstr = std::ffi::CString::new(req_cnf.as_str()).unwrap_or_default();
        freerdp_sys::freerdp_client_get_aad_url(
            cctx,
            token_request_type,
            scope_cstr.as_ptr(),
            code_cstr.as_ptr(),
            req_cnf_cstr.as_ptr(),
        )
    } else {
        freerdp_sys::freerdp_client_get_aad_url(cctx, token_request_type, code_cstr.as_ptr())
    };

    if token_request_ptr.is_null() {
        error!("get_access_token_cb: failed to build token request");
        return 0;
    }

    // If the AAD handler rewrote the redirect_uri to localhost, patch the token request
    // to use the same URI (must match what was used in the auth request).
    let token_request_str = ptr_to_string(token_request_ptr);
    libc::free(token_request_ptr as *mut std::ffi::c_void);

    let token_request_final = if !response.actual_redirect_uri.is_empty() {
        replace_redirect_uri_in_request(&token_request_str, &response.actual_redirect_uri)
    } else {
        token_request_str
    };

    // Exchange authorization code for access token via FreeRDP HTTP client
    let request_cstr = std::ffi::CString::new(token_request_final.as_str()).unwrap_or_default();
    let result =
        freerdp_sys::client_common_get_access_token(instance, request_cstr.as_ptr(), token);

    if result != 0 && !(*token).is_null() {
        info!("get_access_token_cb: successfully obtained access token");

        // Cache the token
        if let Some(ref cm_arc) = state.config_manager {
            if let Ok(cm) = cm_arc.lock() {
                let expires = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map(|d| d.as_secs() as i64 + 3600)
                    .unwrap_or(0);
                cm.set_cached_token(&cache_key, &scope, &ptr_to_string(*token), expires);
            }
        }
        return 1;
    }

    error!(
        "get_access_token_cb: token exchange failed for {}",
        cache_hostname
    );
    0
}

/// Extract an authorization code from an OAuth redirect URL.
/// Looks for `code=<value>` in the query string.
fn extract_code_from_url(url: &str) -> String {
    url.split('?')
        .nth(1)
        .unwrap_or("")
        .split('&')
        .find_map(|param| {
            let (key, value) = param.split_once('=')?;
            if key == "code" {
                Some(value.to_string())
            } else {
                None
            }
        })
        .unwrap_or_default()
}

/// Replace the `redirect_uri` parameter in a URL-encoded token request body.
fn replace_redirect_uri_in_request(request: &str, new_redirect_uri: &str) -> String {
    // Token request is URL-encoded form data: param1=val1&param2=val2
    let encoded_uri = urlencoding_encode(new_redirect_uri);
    let mut result = String::new();
    let mut replaced = false;
    for part in request.split('&') {
        if !result.is_empty() {
            result.push('&');
        }
        if part.starts_with("redirect_uri=") && !replaced {
            result.push_str("redirect_uri=");
            result.push_str(&encoded_uri);
            replaced = true;
        } else {
            result.push_str(part);
        }
    }
    result
}

/// Minimal percent-encoding for redirect URIs in token requests.
fn urlencoding_encode(s: &str) -> String {
    let mut out = String::with_capacity(s.len() * 3);
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char);
            }
            _ => {
                out.push_str(&format!("%{:02X}", b));
            }
        }
    }
    out
}

// ── RDPLauncher ─────────────────────────────────────────────────────────

/// Manages FreeRDP sessions.
pub struct RDPLauncher {
    sessions: HashMap<String, Arc<Mutex<RDPSession>>>,
    dialog_manager: Option<Arc<DialogManager>>,
    aad_handler: Option<Arc<AADAuthHandler>>,
    config_manager: Option<Arc<Mutex<ConfigManager>>>,
}

impl RDPLauncher {
    pub fn new() -> Self {
        Self {
            sessions: HashMap::new(),
            dialog_manager: None,
            aad_handler: None,
            config_manager: None,
        }
    }

    /// Set the dialog manager for certificate/auth callbacks.
    pub fn set_dialog_manager(&mut self, dm: Arc<DialogManager>) {
        self.dialog_manager = Some(dm);
    }

    /// Set the AAD auth handler for Entra ID OAuth popup flows.
    pub fn set_aad_handler(&mut self, handler: Arc<AADAuthHandler>) {
        self.aad_handler = Some(handler);
    }

    /// Set the config manager for token cache access.
    pub fn set_config_manager(&mut self, cm: Arc<Mutex<ConfigManager>>) {
        self.config_manager = Some(cm);
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
                        aad_handler: self.aad_handler.clone(),
                        config_manager: self.config_manager.clone(),
                        gateway_hostname: profile.gateway_hostname.clone(),
                        use_manual_code_flow: profile.use_manual_code_flow,
                    });
                    get_session_map()
                        .lock()
                        .unwrap()
                        .insert(instance as usize, cb_state);

                    (*instance).VerifyCertificateEx = Some(verify_certificate_cb);
                    (*instance).Authenticate = Some(authenticate_cb);
                    (*instance).GatewayAuthenticate = Some(gateway_authenticate_cb);

                    // Install AAD access token callback (variadic fn ptr — transmute required).
                    // The target type is platform-dependent (BOOL size varies), so we
                    // suppress the annotation lint rather than hardcoding a type.
                    #[allow(clippy::missing_transmute_annotations)]
                    {
                        (*instance).GetAccessToken = std::mem::transmute(
                            get_access_token_cb
                                as unsafe extern "C" fn(
                                    *mut freerdp_sys::freerdp,
                                    freerdp_sys::AccessTokenType,
                                    *mut *mut std::os::raw::c_char,
                                    usize,
                                    *const std::os::raw::c_char,
                                    *const std::os::raw::c_char,
                                )
                                    -> freerdp_sys::BOOL,
                        );
                    }

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
            let native_window = Arc::new(AtomicU64::new(0));
            let nw_clone = Arc::clone(&native_window);
            let sid = session_id.to_string();
            let host = profile.hostname.clone();
            let parent_hwnd = parent_window_id.unwrap_or(0) as usize;

            // Background thread waits for the FreeRDP session to finish
            let thread = std::thread::spawn(move || {
                let ctx = context_addr as *mut freerdp_sys::rdpContext;
                info!("Session {} ({}) — waiting for FreeRDP thread", sid, host);

                state_clone.store(
                    state_to_u8(RDPConnectionState::Connected),
                    Ordering::Relaxed,
                );

                // On Windows, poll for the FreeRDP HWND (class "wfreerdp").
                // As soon as we find it, immediately reparent into the WebUI
                // container so the window never appears as a standalone popup.
                #[cfg(target_os = "windows")]
                {
                    use crate::window_embedding::{find_freerdp_window, reparent_to_webui};
                    for attempt in 0..200 {
                        if let Some(hwnd) = find_freerdp_window(context_addr) {
                            nw_clone.store(hwnd as u64, Ordering::Relaxed);
                            info!(
                                "Session {} — captured HWND=0x{:x} (attempt {})",
                                sid, hwnd, attempt
                            );
                            if parent_hwnd != 0 {
                                reparent_to_webui(hwnd, parent_hwnd);
                            }
                            break;
                        }
                        std::thread::sleep(std::time::Duration::from_millis(50));
                    }
                }
                #[cfg(not(target_os = "windows"))]
                let _ = (nw_clone, parent_hwnd);

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
        _parent_window_id: Option<u64>, // not set on FreeRDP settings; used by caller for reparenting
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
                    $val as freerdp_sys::BOOL,
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

        // Don't set ParentWindowId — the WebUI HWND can become stale by the
        // time FreeRDP's internal thread calls CreateWindowEx (error 1400).
        // Instead, let FreeRDP create a standalone top-level window and
        // reparent it into the WebUI container afterwards via SetParent.
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

    /// Get the native window handle (HWND) for a session.
    /// Returns 0 if the session doesn't exist or the HWND hasn't been captured yet.
    pub fn get_native_window(&self, session_id: &str) -> u64 {
        self.sessions
            .get(session_id)
            .and_then(|s| s.lock().ok())
            .map(|s| s.get_native_window())
            .unwrap_or(0)
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
                        get_session_map()
                            .lock()
                            .unwrap()
                            .remove(&session.instance_ptr);
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
