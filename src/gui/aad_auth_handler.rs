//! Azure AD / Entra ID OAuth popup authentication handler.
//!
//! Opens a secondary WebUI window for Microsoft OAuth login flows.
//! Supports three modes:
//! - Standard WebView popup (default)
//! - UI manual code flow (per-connection setting)
//! - CLI manual code flow (global flag)
//!
//! Equivalent to: `src/gui/aad_auth_handler.cpp` / `aad_auth_handler.hpp`

use crate::types::{AADAuthRequest, AADAuthResponse, AADAuthType};
use crate::utils;
use log::{debug, error, info, warn};
use std::ffi::CString;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Condvar, Mutex, OnceLock};
use std::time::Duration;

/// Timeout for OAuth flow (5 minutes).
const AUTH_TIMEOUT: Duration = Duration::from_secs(300);

/// Global flag for manual code flow mode.
static MANUAL_CODE_FLOW: AtomicBool = AtomicBool::new(false);

/// Global flag for AAD debug logging.
static AAD_DEBUG: AtomicBool = AtomicBool::new(false);

/// Global instance pointer for WebUI callbacks (static C functions need access).
static INSTANCE: OnceLock<*const AADAuthHandler> = OnceLock::new();

// SAFETY: AADAuthHandler uses interior mutability (Mutex/Condvar) and is only
// accessed from the INSTANCE global via immutable reference.
unsafe impl Send for AADAuthHandler {}
unsafe impl Sync for AADAuthHandler {}

/// Internal mutable state protected by Mutex.
struct AuthState {
    pending: bool,
    navigating_to_oauth: bool,
    callback_complete: bool,
    request: Option<AADAuthRequest>,
    result: AADAuthResponse,
    original_redirect_uri: String,
    actual_redirect_uri: String,
    /// WebUI window handle for the OAuth popup (0 = no window).
    popup_window: usize,
}

impl Default for AuthState {
    fn default() -> Self {
        Self {
            pending: false,
            navigating_to_oauth: false,
            callback_complete: false,
            request: None,
            result: AADAuthResponse {
                success: false,
                redirect_url: String::new(),
                actual_redirect_uri: String::new(),
            },
            original_redirect_uri: String::new(),
            actual_redirect_uri: String::new(),
            popup_window: 0,
        }
    }
}

/// Azure AD OAuth authentication handler.
///
/// Manages the OAuth popup window flow: opens a secondary WebUI window
/// for Microsoft login, intercepts the redirect containing the authorization
/// code, and returns it to FreeRDP.
pub struct AADAuthHandler {
    main_window: Mutex<usize>,
    state: Mutex<AuthState>,
    cv: Condvar,
}

impl AADAuthHandler {
    pub fn new() -> Self {
        let handler = Self {
            main_window: Mutex::new(0),
            state: Mutex::new(AuthState::default()),
            cv: Condvar::new(),
        };
        handler
    }

    /// Register this handler as the global instance for WebUI callbacks.
    /// Must be called once after construction; panics if called twice.
    pub fn register_global(self: &std::sync::Arc<Self>) {
        let ptr: *const AADAuthHandler = std::sync::Arc::as_ptr(self);
        INSTANCE
            .set(ptr)
            .expect("AADAuthHandler already registered");
    }

    /// Enable manual code flow mode (global CLI flag).
    pub fn enable_manual_code_flow() {
        MANUAL_CODE_FLOW.store(true, Ordering::SeqCst);
        info!("AAD: Manual code flow mode enabled");
    }

    /// Check if manual code flow mode is enabled.
    pub fn is_manual_code_flow_enabled() -> bool {
        MANUAL_CODE_FLOW.load(Ordering::SeqCst)
    }

    /// Enable verbose AAD debug logging.
    pub fn enable_debug() {
        AAD_DEBUG.store(true, Ordering::SeqCst);
        info!("AAD: Debug logging enabled (--aad-dbg)");
    }

    /// Check if AAD debug logging is enabled.
    pub fn is_debug_enabled() -> bool {
        AAD_DEBUG.load(Ordering::SeqCst)
    }

    /// Set the main window for toast notifications.
    pub fn set_main_window(&self, window: usize) {
        *self.main_window.lock().unwrap() = window;
    }

    /// Check if an AAD authentication is currently in progress.
    pub fn is_pending(&self) -> bool {
        self.state.lock().unwrap().pending
    }

    /// Handle AAD auth response from JavaScript (embedded flow variant).
    pub fn on_response(&self, success: bool, redirect_url: &str) {
        let mut state = self.state.lock().unwrap();
        let actual = state.actual_redirect_uri.clone();
        state.result = AADAuthResponse {
            success,
            redirect_url: redirect_url.to_string(),
            actual_redirect_uri: actual,
        };
        state.pending = false;
        self.cv.notify_all();
        debug!("AAD: Auth response: {}", if success { "completed" } else { "cancelled" });
    }

    /// Main authentication entry point — called from FreeRDP background thread.
    pub fn handle_authenticate(&self, request: &AADAuthRequest) -> AADAuthResponse {
        let mut state = self.state.lock().unwrap();

        // Initialize state for this request.
        state.pending = true;
        state.navigating_to_oauth = false;
        state.callback_complete = false;
        state.result = AADAuthResponse {
            success: false,
            redirect_url: String::new(),
            actual_redirect_uri: String::new(),
        };
        state.request = Some(request.clone());

        let type_str = match request.auth_type {
            AADAuthType::RdsAad => "RDS_AAD",
            AADAuthType::Avd => "AVD",
        };
        info!("AAD: Authentication requested (type: {type_str})");

        // Extract original redirect URI for later reconstruction.
        state.original_redirect_uri = extract_redirect_uri(&request.auth_url);
        debug!("AAD: Original redirect URI: {}", state.original_redirect_uri);

        if Self::is_debug_enabled() {
            log_url_details(&request.auth_url, "Auth request URL");
        }

        // Manual code flow (CLI mode).
        if Self::is_manual_code_flow_enabled() {
            return self.handle_manual_code_flow(request, state);
        }

        // Per-connection UI manual code flow.
        let main_window = *self.main_window.lock().unwrap();
        if request.use_ui_manual_code_flow && main_window != 0 {
            return self.handle_ui_manual_code_flow(request, main_window, state);
        }

        debug!("AAD: Opening native OAuth popup window...");

        // Notify main window about AAD auth.
        if main_window != 0 {
            let js = format!(
                "showToast('Azure AD authentication required ({type_str}). Opening login window...', 'info', 5000);"
            );
            run_js(main_window, &js);
        }

        // Create OAuth popup window.
        let popup = unsafe { webui_sys::webui_new_window() };
        if popup == 0 {
            error!("AAD: Failed to create popup window");
            state.pending = false;
            return state.result.clone();
        }
        state.popup_window = popup;

        unsafe {
            webui_sys::webui_set_size(popup, 800, 700);
            webui_sys::webui_set_navigate_passthrough(popup, true);

            // Bind all events (empty string = catch-all).
            let empty = CString::new("").unwrap();
            webui_sys::webui_bind(popup, empty.as_ptr(), Some(s_handle_window_events));

            // Bind oauthCallback for JavaScript-based code capture.
            let cb_name = CString::new("oauthCallback").unwrap();
            webui_sys::webui_bind(popup, cb_name.as_ptr(), Some(s_handle_oauth_callback));

            // Bind logToBackend for browser console forwarding.
            let log_name = CString::new("logToBackend").unwrap();
            webui_sys::webui_bind(popup, log_name.as_ptr(), Some(s_handle_log_to_backend));
        }

        // Show placeholder page and get server port.
        let placeholder_html = PLACEHOLDER_HTML;
        let html_cstr = CString::new(placeholder_html).unwrap();
        let shown = unsafe { webui_sys::webui_show(popup, html_cstr.as_ptr()) };

        if !shown {
            error!("AAD: Failed to show auth window");
            state.popup_window = 0;
            state.pending = false;
            return state.result.clone();
        }

        let port = unsafe { webui_sys::webui_get_port(popup) };
        debug!("AAD: Auth callback server on port: {port}");

        if port == 0 {
            error!("AAD: Failed to get server port");
            state.popup_window = 0;
            state.pending = false;
            return state.result.clone();
        }

        // Rewrite redirect URI if needed (e.g. ms-appx-web:// → localhost).
        let modified_url =
            rewrite_auth_url_with_localhost_redirect(&request.auth_url, port as u16);
        let actual = extract_redirect_uri(&modified_url);
        state.actual_redirect_uri = if actual.is_empty() {
            state.original_redirect_uri.clone()
        } else {
            actual
        };
        debug!("AAD: Using redirect URI: {}", state.actual_redirect_uri);

        // Brief pause for WebView initialization.
        drop(state); // Release lock during sleep
        std::thread::sleep(Duration::from_millis(500));
        let mut state = self.state.lock().unwrap();

        // Navigate to the OAuth URL.
        if state.popup_window != 0 {
            debug!("AAD: Navigating webview to auth URL...");
            state.navigating_to_oauth = true;
            let url_cstr = CString::new(modified_url.as_str()).unwrap();
            unsafe { webui_sys::webui_navigate(popup, url_cstr.as_ptr()) };
        } else {
            error!("AAD: Window closed before navigation");
            state.pending = false;
            return state.result.clone();
        }

        // Wait for OAuth completion (5 minute timeout).
        debug!("AAD: Waiting for OAuth completion...");
        let result = self
            .cv
            .wait_timeout_while(state, AUTH_TIMEOUT, |s| s.pending);

        let mut state = match result {
            Ok((s, _)) => s,
            Err(e) => {
                error!("AAD: Condvar poisoned: {e}");
                return AADAuthResponse {
                    success: false,
                    redirect_url: String::new(),
                    actual_redirect_uri: String::new(),
                };
            }
        };

        // Wait for WebUI callback to fully return (avoid use-after-free).
        let popup_to_close = state.popup_window;
        if !state.callback_complete {
            drop(state);
            for _ in 0..100 {
                std::thread::sleep(Duration::from_millis(10));
                let s = self.state.lock().unwrap();
                if s.callback_complete {
                    break;
                }
            }
            // Grace period for WebUI internal cleanup.
            std::thread::sleep(Duration::from_millis(100));
            state = self.state.lock().unwrap();
        }

        // Close the popup (intentionally don't destroy to avoid SEGFAULT).
        if popup_to_close != 0 {
            debug!("AAD: Closing auth window...");
            unsafe { webui_sys::webui_close(popup_to_close) };
            state.popup_window = 0;
        }

        let timed_out = state.result.redirect_url.is_empty() && !state.result.success;
        let auth_result = state.result.clone();
        drop(state);

        // Notify main window about result.
        if main_window != 0 {
            let success = auth_result.success;
            let js = format!(
                "onAADAuthComplete({});",
                if success { "true" } else { "false" }
            );
            run_js(main_window, &js);
            if timed_out {
                error!("AAD: Auth timed out");
            }
        }

        debug!("AAD: handle_authenticate returning, success={}", auth_result.success);
        auth_result
    }

    // ── Manual code flow (CLI) ───────────────────────────────────────────

    fn handle_manual_code_flow(
        &self,
        request: &AADAuthRequest,
        mut state: std::sync::MutexGuard<'_, AuthState>,
    ) -> AADAuthResponse {
        let type_str = match request.auth_type {
            AADAuthType::RdsAad => "RDS_AAD",
            AADAuthType::Avd => "AVD",
        };
        let original_redirect = state.original_redirect_uri.clone();

        eprintln!();
        eprintln!("============================================");
        eprintln!("  MANUAL AZURE AD AUTHENTICATION");
        eprintln!("  Type: {type_str}");
        eprintln!("============================================");
        eprintln!();
        eprintln!("Please open the following URL in your browser:");
        eprintln!();
        eprintln!("{}", request.auth_url);
        eprintln!();
        eprintln!("After completing authentication, you will be redirected to a URL");
        eprintln!("starting with: {original_redirect}");
        eprintln!();
        eprintln!("Copy the ENTIRE redirect URL (including the ?code=... part)");
        eprintln!("and paste it below, then press Enter:");
        eprintln!();
        eprint!("> ");

        // Notify main window.
        let main_window = *self.main_window.lock().unwrap();
        if main_window != 0 {
            let js = format!(
                "showToast('Azure AD authentication required ({type_str}). Check the console for manual authentication instructions.', 'info', 10000);"
            );
            run_js(main_window, &js);
        }

        // Release lock while waiting for stdin.
        drop(state);
        let mut redirect_url = String::new();
        let _ = std::io::stdin().read_line(&mut redirect_url);
        let redirect_url = redirect_url.trim().to_string();
        let mut state = self.state.lock().unwrap();

        if redirect_url.is_empty() {
            warn!("AAD: No URL provided, authentication cancelled");
            state.pending = false;
            state.result = AADAuthResponse {
                success: false,
                redirect_url: String::new(),
                actual_redirect_uri: String::new(),
            };
            return state.result.clone();
        }

        if redirect_url.contains("code=") {
            info!("AAD: Authorization code received (manual flow)");
            state.result = AADAuthResponse {
                success: true,
                redirect_url,
                actual_redirect_uri: original_redirect,
            };
        } else if redirect_url.contains("error=") {
            warn!("AAD: Error in redirect URL");
            state.result = AADAuthResponse {
                success: false,
                redirect_url: String::new(),
                actual_redirect_uri: String::new(),
            };
        } else {
            warn!("AAD: Invalid redirect URL (no code= or error= parameter)");
            state.result = AADAuthResponse {
                success: false,
                redirect_url: String::new(),
                actual_redirect_uri: String::new(),
            };
        }

        state.pending = false;

        // Notify main window.
        if main_window != 0 {
            let js = format!(
                "onAADAuthComplete({});",
                if state.result.success { "true" } else { "false" }
            );
            run_js(main_window, &js);
        }

        state.result.clone()
    }

    // ── UI manual code flow ──────────────────────────────────────────────

    fn handle_ui_manual_code_flow(
        &self,
        request: &AADAuthRequest,
        main_window: usize,
        mut state: std::sync::MutexGuard<'_, AuthState>,
    ) -> AADAuthResponse {
        let type_str = match request.auth_type {
            AADAuthType::RdsAad => "RDS_AAD",
            AADAuthType::Avd => "AVD",
        };
        let original_redirect = state.original_redirect_uri.clone();

        info!(
            "AAD: UI manual code flow for {type_str} (step {}/{})",
            request.step_current, request.step_total
        );

        let json = format!(
            r#"{{"auth_url":"{}","type":"{}","step_current":{},"step_total":{},"step_label":"{}","redirect_uri":"{}"}}"#,
            escape_json(&request.auth_url),
            type_str,
            request.step_current,
            request.step_total,
            escape_json(&request.step_label),
            escape_json(&original_redirect),
        );
        let js = format!("showManualCodeFlowDialog({json});");
        run_js(main_window, &js);

        // Wait for response.
        let result = self
            .cv
            .wait_timeout_while(state, AUTH_TIMEOUT, |s| s.pending);

        state = match result {
            Ok((s, _)) => s,
            Err(_) => {
                return AADAuthResponse {
                    success: false,
                    redirect_url: String::new(),
                    actual_redirect_uri: String::new(),
                };
            }
        };

        if state.result.success && state.result.actual_redirect_uri.is_empty() {
            state.result.actual_redirect_uri = original_redirect;
        }

        if main_window != 0 {
            let js = format!(
                "onAADAuthComplete({});",
                if state.result.success { "true" } else { "false" }
            );
            run_js(main_window, &js);
        }

        state.result.clone()
    }

    // ── Navigation processing (called from WebUI callbacks) ──────────────

    fn process_navigation(&self, url: &str) {
        debug!("AAD: NAVIGATION: {url}");
        if Self::is_debug_enabled() {
            log_url_details(url, "Navigation");
        }

        // Check for nativeclient redirect callback (not just a URL containing "nativeclient"
        // inside the redirect_uri query param of an authorize request).
        let is_nativeclient_callback =
            (url.starts_with("https://login.microsoftonline.com/")
                || url.starts_with("http://login.microsoftonline.com/"))
                && url.contains("/oauth2/nativeclient");

        let has_auth_code = url.contains("code=");
        let has_auth_error = url.contains("error=");

        // Navigation TO the OAuth provider — track so we don't treat disconnect as cancellation.
        if !is_nativeclient_callback
            && (url.contains("login.microsoftonline.com")
                || url.contains("login.microsoft.com")
                || url.contains("login.live.com"))
        {
            debug!("AAD: Navigating to OAuth provider - expecting disconnect");
            if let Ok(mut state) = self.state.lock() {
                state.navigating_to_oauth = true;
            }
            return;
        }

        let is_localhost_callback =
            url.contains("localhost:") && url.contains("/oauth/callback");
        let is_msappx_redirect =
            url.starts_with("ms-appx-web://") || url.contains("ms-appx-web%3A");

        if is_localhost_callback
            || is_nativeclient_callback
            || is_msappx_redirect
            || has_auth_code
            || has_auth_error
        {
            let mut state = match self.state.lock() {
                Ok(s) => s,
                Err(_) => return,
            };

            if has_auth_code {
                info!("AAD: AUTHORIZATION CODE RECEIVED");

                // Reconstruct the original redirect URL if we rewrote it.
                let result_url = if is_localhost_callback
                    && !state.original_redirect_uri.is_empty()
                {
                    let reconstructed =
                        reconstruct_original_redirect(url, &state.original_redirect_uri);
                    debug!("AAD: Reconstructed redirect URL: {reconstructed}");
                    reconstructed
                } else {
                    url.to_string()
                };

                let actual = state.actual_redirect_uri.clone();
                state.result = AADAuthResponse {
                    success: true,
                    redirect_url: result_url,
                    actual_redirect_uri: actual,
                };
            } else if has_auth_error {
                warn!("AAD: Authentication error in redirect");
                state.result = AADAuthResponse {
                    success: false,
                    redirect_url: String::new(),
                    actual_redirect_uri: String::new(),
                };
            } else {
                // Intermediate redirect without code/error — keep waiting.
                debug!("AAD: Redirect without code or error yet, continuing flow");
                return;
            }

            state.pending = false;
            self.cv.notify_all();
        }
    }

    fn handle_disconnected(&self) {
        debug!("AAD: DISCONNECTED event received");
        let mut state = match self.state.lock() {
            Ok(s) => s,
            Err(_) => return,
        };
        if state.navigating_to_oauth {
            debug!("AAD: Expected disconnect during OAuth flow");
            return;
        }
        info!("AAD: Window closed by user");
        if state.pending {
            state.result = AADAuthResponse {
                success: false,
                redirect_url: String::new(),
                actual_redirect_uri: String::new(),
            };
            state.pending = false;
            self.cv.notify_all();
        }
    }
}

impl Drop for AADAuthHandler {
    fn drop(&mut self) {
        let state = self.state.lock().unwrap();
        if state.popup_window != 0 {
            unsafe {
                webui_sys::webui_close(state.popup_window);
            }
        }
    }
}

// ── Static WebUI callbacks ───────────────────────────────────────────────

fn get_instance() -> Option<&'static AADAuthHandler> {
    INSTANCE.get().map(|ptr| unsafe { &**ptr })
}

unsafe extern "C" fn s_handle_window_events(e: *mut webui_sys::webui_event_t) {
    let Some(handler) = get_instance() else { return };
    let event_type = unsafe { (*e).event_type };
    // webui_event enum: DISCONNECTED=0, CONNECTED=1, MOUSE_CLICK=2, NAVIGATION=3
    const NAVIGATION: usize = 3;
    const DISCONNECTED: usize = 0;
    if event_type == NAVIGATION {
        let url_ptr = unsafe { webui_sys::webui_get_string_at(e, 0) };
        if !url_ptr.is_null() {
            let url = unsafe { std::ffi::CStr::from_ptr(url_ptr) }
                .to_string_lossy()
                .to_string();
            handler.process_navigation(&url);
            if !handler.is_pending() {
                if let Ok(mut s) = handler.state.lock() {
                    s.callback_complete = true;
                }
            }
        }
    } else if event_type == DISCONNECTED {
        handler.handle_disconnected();
        if !handler.is_pending() {
            if let Ok(mut s) = handler.state.lock() {
                s.callback_complete = true;
            }
        }
    }
}

unsafe extern "C" fn s_handle_oauth_callback(e: *mut webui_sys::webui_event_t) {
    let Some(handler) = get_instance() else { return };
    let url_ptr = unsafe { webui_sys::webui_get_string_at(e, 0) };
    if !url_ptr.is_null() {
        let url = unsafe { std::ffi::CStr::from_ptr(url_ptr) }
            .to_string_lossy()
            .to_string();
        debug!("AAD: OAuth callback received via JavaScript");
        handler.process_navigation(&url);
    }
}

unsafe extern "C" fn s_handle_log_to_backend(e: *mut webui_sys::webui_event_t) {
    let msg_ptr = unsafe { webui_sys::webui_get_string_at(e, 0) };
    if !msg_ptr.is_null() {
        let msg = unsafe { std::ffi::CStr::from_ptr(msg_ptr) }
            .to_string_lossy();
        debug!("AAD: {msg}");
    }
}

// ── URL helpers ──────────────────────────────────────────────────────────

/// Extract the redirect_uri parameter from an OAuth URL.
fn extract_redirect_uri(auth_url: &str) -> String {
    // Match redirect_uri=<value> up to the next & or end.
    if let Some(start) = auth_url
        .find("redirect_uri=")
        .map(|i| i + "redirect_uri=".len())
    {
        let rest = &auth_url[start..];
        let end = rest.find('&').unwrap_or(rest.len());
        utils::url_decode(&rest[..end])
    } else {
        String::new()
    }
}

/// Rewrite OAuth URL redirect URI when needed (e.g. ms-appx-web:// → localhost).
fn rewrite_auth_url_with_localhost_redirect(auth_url: &str, port: u16) -> String {
    let original = extract_redirect_uri(auth_url);
    if original.is_empty() {
        return auth_url.to_string();
    }

    // Already localhost? No rewrite needed.
    if original.starts_with("http://localhost") || original.starts_with("http://127.0.0.1") {
        return auth_url.to_string();
    }

    // Keep nativeclient redirect as-is to avoid AADSTS50011 mismatch.
    if original.contains("/oauth2/nativeclient") {
        return auth_url.to_string();
    }

    // Rewrite unsupported redirect schemes to localhost callback.
    let new_redirect = format!("http://localhost:{port}/oauth/callback");
    let new_encoded = utils::url_encode(&new_redirect);

    // Replace redirect_uri=<old> with redirect_uri=<new>.
    if let Some(start) = auth_url.find("redirect_uri=") {
        let param_start = start + "redirect_uri=".len();
        let rest = &auth_url[param_start..];
        let end = rest.find('&').unwrap_or(rest.len());
        format!(
            "{}redirect_uri={}{}",
            &auth_url[..start],
            new_encoded,
            &auth_url[param_start + end..]
        )
    } else {
        auth_url.to_string()
    }
}

/// Reconstruct the original redirect URL from a localhost callback.
fn reconstruct_original_redirect(callback_url: &str, original_redirect: &str) -> String {
    if let Some(qpos) = callback_url.find('?') {
        let query = &callback_url[qpos..];
        format!("{original_redirect}{query}")
    } else {
        original_redirect.to_string()
    }
}

/// Escape a string for embedding in a JSON string value.
fn escape_json(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 16);
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            _ => out.push(c),
        }
    }
    out
}

/// Execute JavaScript on a WebUI window (fire-and-forget).
fn run_js(window: usize, js: &str) {
    if let Ok(cstr) = CString::new(js) {
        unsafe { webui_sys::webui_run(window, cstr.as_ptr()) };
    }
}

/// Log URL details for debug purposes.
fn log_url_details(url: &str, context: &str) {
    debug!("AAD: --- {context} ---");
    debug!("AAD: Full URL: {url}");

    if let Some(scheme_end) = url.find("://") {
        debug!("AAD: Scheme: {}", &url[..scheme_end]);
        let host_start = scheme_end + 3;
        let path_start = url[host_start..].find('/').map(|i| i + host_start);
        let query_start = url[host_start..].find('?').map(|i| i + host_start);
        let host_end = match (path_start, query_start) {
            (Some(a), Some(b)) => a.min(b),
            (Some(a), None) | (None, Some(a)) => a,
            (None, None) => url.len(),
        };
        debug!("AAD: Host: {}", &url[host_start..host_end]);
    }

    if let Some(qpos) = url.find('?') {
        let mut query = &url[qpos + 1..];
        if let Some(frag) = query.find('#') {
            query = &query[..frag];
        }
        debug!("AAD: Query parameters:");
        for param in query.split('&') {
            if let Some(eq) = param.find('=') {
                let key = &param[..eq];
                let val = utils::url_decode(&param[eq + 1..]);
                if key == "code" || key == "client_secret" {
                    let truncated = if val.len() > 8 { &val[..8] } else { &val };
                    debug!("AAD:   {key} = {truncated}...");
                } else {
                    debug!("AAD:   {key} = {val}");
                }
            }
        }
    }
    debug!("AAD: --- end {context} ---");
}

/// Placeholder HTML shown while the OAuth popup initializes.
const PLACEHOLDER_HTML: &str = r#"<!DOCTYPE html>
<html>
<head>
<title>Azure AD Authentication</title>
<script src="webui.js"></script>
<script>
(function() {
  var originalLog = console.log;
  console.log = function() {
    var msg = Array.prototype.slice.call(arguments).join(' ');
    originalLog.apply(console, arguments);
    if (typeof logToBackend !== 'undefined') {
      logToBackend('[AAD-Browser] ' + msg);
    }
  };
})();
window.onload = function() {
  var url = window.location.href;
  console.log('AAD Auth window loaded');
  if (url.indexOf('code=') !== -1 || url.indexOf('error=') !== -1) {
    console.log('OAuth callback detected!');
    if (typeof oauthCallback !== 'undefined') {
      oauthCallback(url);
    }
  }
};
</script>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
       display: flex; flex-direction: column; align-items: center; justify-content: center;
       height: 100vh; margin: 0; background: #f5f5f5; }
h1 { color: #333; }
.spinner { width: 50px; height: 50px; border: 5px solid #e0e0e0;
           border-top: 5px solid #0078d4; border-radius: 50%; animation: spin 1s linear infinite; }
@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
</style>
</head>
<body>
<h1>Azure AD Authentication</h1>
<div class="spinner"></div>
<p>Setting up authentication...</p>
</body>
</html>"#;

