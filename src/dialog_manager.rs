//! Thread-safe dialog synchronization for certificate and authentication prompts.
//!
//! FreeRDP callbacks run on background session threads but UI updates must
//! happen on the WebUI thread. This module bridges the gap using
//! `Mutex` + `Condvar` wait-notify patterns with timeouts.
//!
//! Equivalent to: `src/dialog_manager.cpp` / `dialog_manager.hpp`

use crate::types::{AuthRequest, AuthResponse, CertificateAcceptance, CertificateInfo};
use log::{info, warn};
use std::ffi::CString;
use std::sync::{Condvar, Mutex};
use std::time::Duration;

const DIALOG_TIMEOUT_SECS: u64 = 120;

struct DialogState {
    cert_pending: bool,
    cert_result: CertificateAcceptance,
    auth_pending: bool,
    auth_result: AuthResponse,
}

/// Synchronizes FreeRDP callback threads with the WebUI dialog layer.
///
/// When FreeRDP needs certificate verification or authentication, the session
/// thread calls `handle_certificate_verify()` or `handle_authenticate()`, which:
/// 1. Sends a JavaScript call to show the dialog in the React UI
/// 2. Blocks the session thread until the user responds (or 120s timeout)
///
/// The UI response arrives via `on_certificate_response()` / `on_auth_response()`,
/// which unblock the waiting session thread.
pub struct DialogManager {
    window: usize,
    state: Mutex<DialogState>,
    cv: Condvar,
}

impl DialogManager {
    pub fn new(window: usize) -> Self {
        Self {
            window,
            state: Mutex::new(DialogState {
                cert_pending: false,
                cert_result: CertificateAcceptance::Reject,
                auth_pending: false,
                auth_result: AuthResponse {
                    success: false,
                    username: String::new(),
                    password: String::new(),
                    domain: String::new(),
                },
            }),
            cv: Condvar::new(),
        }
    }

    /// Called from the FreeRDP session thread when a certificate needs verification.
    /// Blocks until the user responds via the UI or the timeout expires.
    pub fn handle_certificate_verify(&self, info: &CertificateInfo) -> CertificateAcceptance {
        let mut state = self.state.lock().unwrap();
        state.cert_pending = true;
        state.cert_result = CertificateAcceptance::Reject;

        info!(
            "Certificate verification requested for {}:{} (fingerprint: {})",
            info.host, info.port, info.fingerprint
        );

        let js = format!(
            r#"showCertificateDialog({{"host":"{}","port":{},"commonName":"{}","subject":"{}","issuer":"{}","fingerprint":"{}","isChanged":{},"oldFingerprint":"{}"}})"#,
            escape_json(&info.host),
            info.port,
            escape_json(&info.common_name),
            escape_json(&info.subject),
            escape_json(&info.issuer),
            escape_json(&info.fingerprint),
            info.is_changed,
            escape_json(&info.old_fingerprint)
        );

        if let Ok(js_cstr) = CString::new(js) {
            unsafe {
                webui_sys::webui_run(self.window, js_cstr.as_ptr());
            }
        }

        let result =
            self.cv
                .wait_timeout_while(state, Duration::from_secs(DIALOG_TIMEOUT_SECS), |s| {
                    s.cert_pending
                });

        match result {
            Ok((ref state, ref timeout)) => {
                if timeout.timed_out() {
                    warn!(
                        "Certificate dialog timed out after {}s — rejecting",
                        DIALOG_TIMEOUT_SECS
                    );
                    CertificateAcceptance::Reject
                } else {
                    info!("Certificate dialog response: {:?}", state.cert_result);
                    state.cert_result
                }
            }
            Err(_) => {
                warn!("Certificate dialog mutex poisoned — rejecting");
                CertificateAcceptance::Reject
            }
        }
    }

    /// Called from the FreeRDP session thread when credentials are needed.
    /// Blocks until the user responds via the UI or the timeout expires.
    pub fn handle_authenticate(&self, request: &AuthRequest) -> AuthResponse {
        let mut state = self.state.lock().unwrap();
        state.auth_pending = true;
        state.auth_result = AuthResponse {
            success: false,
            username: String::new(),
            password: String::new(),
            domain: String::new(),
        };

        info!(
            "Authentication requested for {} (gateway: {})",
            request.hostname, request.is_gateway
        );

        let js = format!(
            r#"showAuthDialog({{"hostname":"{}","isGateway":{},"currentUsername":"{}","currentDomain":"{}"}})"#,
            escape_json(&request.hostname),
            request.is_gateway,
            escape_json(&request.current_username),
            escape_json(&request.current_domain)
        );

        if let Ok(js_cstr) = CString::new(js) {
            unsafe {
                webui_sys::webui_run(self.window, js_cstr.as_ptr());
            }
        }

        let result =
            self.cv
                .wait_timeout_while(state, Duration::from_secs(DIALOG_TIMEOUT_SECS), |s| {
                    s.auth_pending
                });

        match result {
            Ok((ref state, ref timeout)) => {
                if timeout.timed_out() {
                    warn!(
                        "Auth dialog timed out after {}s — failing",
                        DIALOG_TIMEOUT_SECS
                    );
                    AuthResponse {
                        success: false,
                        username: String::new(),
                        password: String::new(),
                        domain: String::new(),
                    }
                } else {
                    info!(
                        "Auth dialog response: success={}",
                        state.auth_result.success
                    );
                    state.auth_result.clone()
                }
            }
            Err(_) => {
                warn!("Auth dialog mutex poisoned — failing");
                AuthResponse {
                    success: false,
                    username: String::new(),
                    password: String::new(),
                    domain: String::new(),
                }
            }
        }
    }

    /// Called from the JS handler when the user responds to a certificate dialog.
    pub fn on_certificate_response(&self, choice: i32) {
        let mut state = self.state.lock().unwrap();
        state.cert_result = match choice {
            1 => CertificateAcceptance::AcceptPermanently,
            2 => CertificateAcceptance::AcceptTemporarily,
            _ => CertificateAcceptance::Reject,
        };
        state.cert_pending = false;
        info!("Certificate response received: {:?}", state.cert_result);
        self.cv.notify_all();
    }

    /// Called from the JS handler when the user responds to an auth dialog.
    pub fn on_auth_response(&self, success: bool, username: &str, password: &str, domain: &str) {
        let mut state = self.state.lock().unwrap();
        state.auth_result = AuthResponse {
            success,
            username: username.to_string(),
            password: password.to_string(),
            domain: domain.to_string(),
        };
        state.auth_pending = false;
        info!("Auth response received: success={}", success);
        self.cv.notify_all();
    }
}

/// Escape special characters for safe embedding in JSON string values.
fn escape_json(s: &str) -> String {
    s.replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('\n', "\\n")
        .replace('\r', "\\r")
        .replace('\t', "\\t")
}
