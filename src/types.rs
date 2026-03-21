//! Shared data structures and callback types.
//!
//! Defines all domain types used across the application, matching the
//! C++ `connection_types.hpp`.
//!
//! Equivalent to: `src/connection_types.hpp`

use serde::{Deserialize, Serialize};

/// RDP connection profile stored in the database.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ConnectionProfile {
    pub name: String,
    pub folder: String,
    pub hostname: String,
    #[serde(default = "default_port")]
    pub port: u16,
    #[serde(default)]
    pub username: String,
    #[serde(default)]
    pub domain: String,
    // Display
    #[serde(default = "default_width")]
    pub width: u32,
    #[serde(default = "default_height")]
    pub height: u32,
    #[serde(default)]
    pub fullscreen: bool,
    // Features
    #[serde(default)]
    pub home_drive: bool,
    #[serde(default)]
    pub clipboard: bool,
    #[serde(default)]
    pub cert_tofu: bool,
    #[serde(default)]
    pub usb_auto: bool,
    #[serde(default)]
    pub floatbar: bool,
    #[serde(default)]
    pub dynamic_resolution: bool,
    #[serde(default)]
    pub network_auto: bool,
    #[serde(default)]
    pub gfx_avc420: bool,
    #[serde(default)]
    pub compression: bool,
    #[serde(default)]
    pub audio_pulse: bool,
    #[serde(default)]
    pub prevent_session_lock: bool,
    #[serde(default = "default_true")]
    pub auto_reconnect: bool,
    #[serde(default = "default_retries")]
    pub auto_reconnect_max_retries: i32,
    // Gateway
    #[serde(default)]
    pub gateway_hostname: String,
    // AAD / AVD
    #[serde(default)]
    pub enable_rds_aad_auth: bool,
    #[serde(default)]
    pub target_is_aad_joined: bool,
    #[serde(default)]
    pub load_balance_info: String,
    #[serde(default)]
    pub use_manual_code_flow: bool,
    #[serde(default)]
    pub aad_tenant_id: String,
    #[serde(default)]
    pub wvd_endpoint_pool: String,
    #[serde(default)]
    pub workspace_id: String,
    #[serde(default)]
    pub arm_path: String,
    #[serde(default)]
    pub remote_application_program: String,
    #[serde(default)]
    pub remote_desktop_name: String,
    #[serde(default)]
    pub source_account_id: String,
    /// Fields explicitly set on this connection (vs inherited from folder).
    #[serde(default)]
    pub overridden_fields: Vec<String>,
}

fn default_port() -> u16 {
    3389
}
fn default_width() -> u32 {
    1920
}
fn default_height() -> u32 {
    1080
}
fn default_true() -> bool {
    true
}
fn default_retries() -> i32 {
    3
}

/// Folder-level settings for parameter inheritance.
/// All fields are optional — only set fields override parent values.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct FolderSettings {
    pub home_drive: Option<bool>,
    pub clipboard: Option<bool>,
    pub cert_tofu: Option<bool>,
    pub usb_auto: Option<bool>,
    pub floatbar: Option<bool>,
    pub dynamic_resolution: Option<bool>,
    pub network_auto: Option<bool>,
    pub gfx_avc420: Option<bool>,
    pub compression: Option<bool>,
    pub audio_pulse: Option<bool>,
    pub prevent_session_lock: Option<bool>,
    pub auto_reconnect: Option<bool>,
    pub auto_reconnect_max_retries: Option<i32>,
    pub gateway_hostname: Option<String>,
    pub enable_rds_aad_auth: Option<bool>,
    pub target_is_aad_joined: Option<bool>,
    pub load_balance_info: Option<String>,
    pub use_manual_code_flow: Option<bool>,
}

/// Certificate information presented during TLS handshake.
#[derive(Debug, Clone)]
pub struct CertificateInfo {
    pub host: String,
    pub port: u16,
    pub common_name: String,
    pub subject: String,
    pub issuer: String,
    pub fingerprint: String,
    pub is_changed: bool,
    pub old_fingerprint: String,
}

/// User's response to a certificate verification dialog.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum CertificateAcceptance {
    Reject = 0,
    AcceptPermanently = 1,
    AcceptTemporarily = 2,
}

/// Authentication request from FreeRDP (credentials prompt).
#[derive(Debug, Clone)]
pub struct AuthRequest {
    pub hostname: String,
    pub is_gateway: bool,
    pub current_username: String,
    pub current_domain: String,
}

/// User's response to an authentication dialog.
#[derive(Debug, Clone)]
pub struct AuthResponse {
    pub success: bool,
    pub username: String,
    pub password: String,
    pub domain: String,
}

/// AAD OAuth authentication request type.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AADAuthType {
    RdsAad,
    Avd,
}

/// AAD OAuth authentication request from FreeRDP.
#[derive(Debug, Clone)]
pub struct AADAuthRequest {
    pub auth_type: AADAuthType,
    pub auth_url: String,
    pub scope: String,
    pub req_cnf: String,
    pub use_ui_manual_code_flow: bool,
    pub step_current: i32,
    pub step_total: i32,
    pub step_label: String,
}

/// AAD OAuth authentication response.
#[derive(Debug, Clone)]
pub struct AADAuthResponse {
    pub success: bool,
    pub redirect_url: String,
    pub actual_redirect_uri: String,
}

/// WVD feed account metadata.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FeedAccount {
    pub id: String,
    pub display_name: String,
    pub email: String,
    pub refresh_token: String,
    pub last_synced: i64,
}

/// RDP session connection state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RDPConnectionState {
    Idle,
    Connecting,
    Connected,
    Disconnecting,
    Disconnected,
    Error,
}

// Callback types
pub type CertificateVerifyCallback =
    Box<dyn Fn(&CertificateInfo) -> CertificateAcceptance + Send + Sync>;
pub type AuthenticateCallback = Box<dyn Fn(&AuthRequest) -> AuthResponse + Send + Sync>;
pub type AADAuthCallback = Box<dyn Fn(&AADAuthRequest) -> AADAuthResponse + Send + Sync>;
pub type TokenCacheLookupCallback = Box<dyn Fn(&str, &str) -> Option<String> + Send + Sync>;
pub type TokenCacheStoreCallback = Box<dyn Fn(&str, &str, &str, i64) -> bool + Send + Sync>;
