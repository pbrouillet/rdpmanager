//! WVD/AVD feed discovery and import.
//!
//! Authenticates via OAuth2 PKCE flow, discovers Azure Virtual Desktop
//! tenants and resources, downloads RDP files, and imports them as
//! connection profiles.
//!
//! Flow: authenticate_popup() -> discover_tenants() -> fetch_resources() ->
//!       fetch_rdp_content() -> import_resource()
//!
//! Equivalent to: `src/feed_discovery.cpp` / `feed_discovery.hpp`

use std::ffi::CString;
use std::io::Read;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex, OnceLock};
use std::time::Duration;

use log::{error, info, warn};
use quick_xml::events::Event;
use quick_xml::Reader;
use sha2::{Digest, Sha256};

use crate::config_manager::ConfigManager;
use crate::utils;

// ============================================================================
// WVD API constants
// ============================================================================

/// WVD webclient URL (used as redirect target during OAuth flow).
pub const WVD_WEBCLIENT_URL: &str = "https://client.wvd.microsoft.com/arm/webclient/index.html";

/// WVD feed discovery endpoint -- returns tenant/workspace list as XML.
pub const WVD_FEED_DISCOVERY_URL: &str = "https://client.wvd.microsoft.com/api/arm/feeddiscovery";

/// Azure app registration client ID for the WVD webclient.
pub const WVD_CLIENT_ID: &str = "a85cf173-4192-42f8-81fa-777a763e6e2c";

/// OAuth redirect URI -- must match the app registration.
pub const WVD_REDIRECT_URI: &str = "https://client.wvd.microsoft.com/arm/webclient/index.html";

/// OAuth scopes requested during authentication.
pub const WVD_SCOPE: &str = "https://www.wvd.microsoft.com/.default openid profile offline_access";

/// Microsoft identity platform token endpoint (common tenant).
pub const MS_TOKEN_ENDPOINT: &str = "https://login.microsoftonline.com/common/oauth2/v2.0/token";

/// Microsoft identity platform authorize endpoint (common tenant).
pub const MS_AUTHORIZE_ENDPOINT: &str =
    "https://login.microsoftonline.com/common/oauth2/v2.0/authorize";

/// Authentication timeout in seconds.
pub const AUTH_TIMEOUT_SECS: u64 = 300;

// ============================================================================
// Data structures
// ============================================================================

/// A single tenant/workspace discovered from the WVD feed discovery endpoint.
pub struct TenantFeed {
    pub tenant_id: String,
    pub tenant_display_name: String,
    pub workspace_name: String,
    pub feed_url: String,
    pub geo: String,
    pub arm_path: String,
    pub hub_discovery_url: String,
}

/// A single resource (desktop or remote app) within a tenant workspace.
pub struct FeedResource {
    pub id: String,
    pub title: String,
    /// `"RemoteApp"` or `"Desktop"`.
    pub resource_type: String,
    pub arm_path: String,
    pub rdp_url: String,
    pub icon_url: Option<String>,
    pub publisher_name: String,
}

/// Result of a complete discover-and-import operation.
pub struct FeedDiscoveryResult {
    pub success: bool,
    pub error: Option<String>,
    pub imported_count: usize,
    pub tenant_count: usize,
    /// Account identifier (populated after authentication).
    pub account_id: String,
    pub account_display_name: String,
}

impl Default for FeedDiscoveryResult {
    fn default() -> Self {
        Self {
            success: false,
            error: None,
            imported_count: 0,
            tenant_count: 0,
            account_id: String::new(),
            account_display_name: String::new(),
        }
    }
}

/// Progress callback: `(message, current, total)`.
pub type FeedProgressCallback = Box<dyn Fn(&str, usize, usize) + Send + Sync>;

// ============================================================================
// Popup synchronization state
// ============================================================================

/// Global instance pointer for WebUI static callbacks.
static FEED_INSTANCE: OnceLock<*const FeedDiscoveryManager> = OnceLock::new();

// SAFETY: FeedDiscoveryManager uses interior mutability (Mutex/Condvar) and is
// only accessed from the FEED_INSTANCE global via immutable reference.
unsafe impl Send for FeedDiscoveryManager {}
unsafe impl Sync for FeedDiscoveryManager {}

/// Internal mutable state for the popup OAuth flow.
struct PopupState {
    auth_code: String,
    code_verifier: String,
    id_token: String,
    token_found: bool,
    popup_closed: bool,
    popup_window: usize,
}

impl Default for PopupState {
    fn default() -> Self {
        Self {
            auth_code: String::new(),
            code_verifier: String::new(),
            id_token: String::new(),
            token_found: false,
            popup_closed: false,
            popup_window: 0,
        }
    }
}

// ============================================================================
// FeedDiscoveryManager
// ============================================================================

/// Manages the complete WVD feed discovery workflow:
/// 1. Open a popup to the WVD webclient for authentication
/// 2. Intercept the OAuth authorization code from the redirect
/// 3. Exchange the code for an access token via PKCE
/// 4. Discover tenant feeds from the WVD feed discovery endpoint
/// 5. Fetch resources from each tenant
/// 6. Download RDP file content and import into the database
pub struct FeedDiscoveryManager {
    main_window: usize,
    config_manager: Arc<Mutex<ConfigManager>>,
    busy: AtomicBool,
    navigating: AtomicBool,
    popup_state: Mutex<PopupState>,
    popup_cv: Condvar,
}

impl FeedDiscoveryManager {
    /// Create a new `FeedDiscoveryManager`.
    pub fn new(config_manager: Arc<Mutex<ConfigManager>>) -> Self {
        Self {
            main_window: 0,
            config_manager,
            busy: AtomicBool::new(false),
            navigating: AtomicBool::new(false),
            popup_state: Mutex::new(PopupState::default()),
            popup_cv: Condvar::new(),
        }
    }

    /// Set the main WebUI window handle (used for JS progress notifications).
    pub fn set_main_window(&mut self, window: usize) {
        self.main_window = window;
    }

    /// Register this manager as the global instance for WebUI callbacks.
    pub fn register_global(self: &Arc<Self>) {
        let ptr: *const FeedDiscoveryManager = Arc::as_ptr(self);
        let _ = FEED_INSTANCE.set(ptr);
    }

    /// Returns `true` if a discovery operation is currently in progress.
    pub fn is_busy(&self) -> bool {
        self.busy.load(Ordering::Relaxed)
    }

    // ========================================================================
    // G6: Orchestrator -- discover_and_import
    // ========================================================================

    /// Run the complete feed discovery flow: authenticate -> discover -> import.
    pub fn discover_and_import(&self, account_id: &str) -> FeedDiscoveryResult {
        let mut result = FeedDiscoveryResult::default();

        if self.busy.swap(true, Ordering::SeqCst) {
            result.error = Some("Feed discovery is already in progress".into());
            return result;
        }

        // Step 1: Authenticate
        self.notify_progress("Authenticating...", 0, 0);
        let token = match self.authenticate_popup() {
            Ok(t) => t,
            Err(e) => {
                self.busy.store(false, Ordering::SeqCst);
                result.error = Some(format!("Authentication failed: {e}"));
                return result;
            }
        };

        // Step 2: Extract identity from the OIDC id_token
        let id_token = {
            let state = self.popup_state.lock().unwrap();
            state.id_token.clone()
        };
        let id_src = if id_token.is_empty() {
            &token
        } else {
            &id_token
        };
        let user_display_name = utils::jwt_extract_field(id_src, "name").unwrap_or_default();
        let user_upn = utils::jwt_extract_field(id_src, "upn").unwrap_or_default();
        let user_email = utils::jwt_extract_field(id_src, "preferred_username")
            .or_else(|| {
                if user_upn.is_empty() {
                    None
                } else {
                    Some(user_upn.clone())
                }
            })
            .unwrap_or_default();
        let display_name = if user_display_name.is_empty() {
            if user_email.is_empty() {
                "WVD Account".to_string()
            } else {
                user_email.clone()
            }
        } else {
            user_display_name
        };

        info!("FeedDiscovery: Authenticated as: {display_name} ({user_email})");

        // Step 3: Create or update the feed account
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_secs() as i64)
            .unwrap_or(0);

        let final_account_id = if account_id.is_empty() {
            let new_id = utils::generate_uuid();
            if let Ok(cm) = self.config_manager.lock() {
                cm.add_feed_account(&new_id, &display_name, &user_email, now);
            }
            info!("FeedDiscovery: Created new account: {new_id}");
            new_id
        } else {
            if let Ok(cm) = self.config_manager.lock() {
                cm.update_feed_account(account_id, &display_name, &user_email, now);
            }
            info!("FeedDiscovery: Updated account: {account_id}");
            account_id.to_string()
        };

        result.account_id = final_account_id.clone();
        result.account_display_name = display_name;

        // Step 4: Discover tenants
        self.notify_progress("Discovering tenant feeds...", 0, 0);
        let tenants = match self.discover_tenants(&token) {
            Ok(t) => t,
            Err(e) => {
                warn!("FeedDiscovery: Tenant discovery failed: {e}");
                self.busy.store(false, Ordering::SeqCst);
                result.success = true;
                result.error = Some(format!("No tenant feeds found: {e}"));
                return result;
            }
        };

        if tenants.is_empty() {
            self.busy.store(false, Ordering::SeqCst);
            result.success = true;
            result.error = Some("No tenant feeds found".into());
            return result;
        }

        result.tenant_count = tenants.len();

        // Step 5: Iterate tenants, fetch resources, import RDP files
        let mut imported = 0usize;

        for (tenant_idx, tenant) in tenants.iter().enumerate() {
            self.notify_progress(
                &format!("Fetching resources from {}...", tenant.workspace_name),
                tenant_idx + 1,
                result.tenant_count,
            );

            info!(
                "FeedDiscovery: Fetching resources for tenant: {}",
                tenant.workspace_name
            );

            let resources = match self.fetch_tenant_resources(&tenant.feed_url, &token) {
                Ok(r) => r,
                Err(e) => {
                    error!(
                        "FeedDiscovery: Failed to fetch resources for {}: {e}",
                        tenant.workspace_name
                    );
                    continue;
                }
            };

            for resource in &resources {
                info!("FeedDiscovery: Downloading RDP for: {}", resource.title);

                let rdp_content = match self.fetch_rdp_content(&resource.rdp_url, &token) {
                    Ok(c) => c,
                    Err(e) => {
                        error!(
                            "FeedDiscovery: Failed to download RDP for {}: {e}",
                            resource.title
                        );
                        continue;
                    }
                };

                match self.import_resource(
                    resource,
                    &tenant.workspace_name,
                    &rdp_content,
                    &final_account_id,
                ) {
                    Ok(()) => {
                        imported += 1;
                        info!(
                            "FeedDiscovery: Imported: {} into Feeds/{}",
                            resource.title, tenant.workspace_name
                        );
                    }
                    Err(e) => {
                        error!("FeedDiscovery: Failed to import {}: {e}", resource.title);
                    }
                }
            }
        }

        result.success = true;
        result.imported_count = imported;

        // Notify completion
        let completion_msg = format!(
            "Imported {} resources from {} workspaces",
            imported, result.tenant_count
        );
        self.notify_progress(&completion_msg, result.tenant_count, result.tenant_count);

        if self.main_window != 0 {
            let js = format!(
                r#"if (typeof onFeedDiscoveryComplete === 'function') onFeedDiscoveryComplete({{"success":true,"imported_count":{},"tenant_count":{},"account_id":"{}","account_display_name":"{}","message":"{}"}});"#,
                imported,
                result.tenant_count,
                json_escape(&result.account_id),
                json_escape(&result.account_display_name),
                json_escape(&completion_msg),
            );
            run_js(self.main_window, &js);
        }

        info!(
            "FeedDiscovery: Discovery complete: {imported} resources imported from {} tenants",
            result.tenant_count
        );

        self.busy.store(false, Ordering::SeqCst);
        result
    }

    // ========================================================================
    // G2: OAuth2 PKCE popup authentication
    // ========================================================================

    /// Open a popup for Microsoft login using PKCE OAuth flow.
    fn authenticate_popup(&self) -> Result<String, String> {
        // Reset popup state
        {
            let mut state = self.popup_state.lock().unwrap();
            *state = PopupState::default();
        }
        self.navigating.store(false, Ordering::SeqCst);

        // Generate PKCE pair
        let code_verifier = utils::generate_code_verifier();
        let hash = Sha256::digest(code_verifier.as_bytes());
        let code_challenge = utils::base64url_encode(&hash);

        // Store code_verifier in popup state
        {
            let mut state = self.popup_state.lock().unwrap();
            state.code_verifier = code_verifier;
        }

        // Build the OAuth2 authorization URL with PKCE
        let auth_url = format!(
            "{}?client_id={}&scope={}&redirect_uri={}&response_type=code&response_mode=fragment&code_challenge={}&code_challenge_method=S256",
            MS_AUTHORIZE_ENDPOINT,
            WVD_CLIENT_ID,
            utils::url_encode(WVD_SCOPE),
            utils::url_encode(WVD_REDIRECT_URI),
            code_challenge,
        );

        info!("FeedDiscovery: PKCE code_challenge: {code_challenge}");
        info!("FeedDiscovery: Auth URL: {auth_url}");

        // Notify main window
        if self.main_window != 0 {
            run_js(
                self.main_window,
                "if (typeof showToast === 'function') showToast('Opening WVD login window...', 'info', 5000);",
            );
        }

        // Create the popup window
        let popup = unsafe { webui_sys::webui_new_window() };
        if popup == 0 {
            return Err("Failed to create popup window".into());
        }

        {
            let mut state = self.popup_state.lock().unwrap();
            state.popup_window = popup;
        }

        unsafe {
            webui_sys::webui_set_size(popup, 900, 750);
            webui_sys::webui_set_navigate_passthrough(popup, true);

            // Bind all events (empty string = catch-all) to intercept navigation
            let empty = CString::new("").unwrap();
            webui_sys::webui_bind(popup, empty.as_ptr(), Some(s_handle_popup_events));
        }

        // Show placeholder page
        let placeholder = CString::new(PLACEHOLDER_HTML).unwrap();
        let shown = unsafe { webui_sys::webui_show(popup, placeholder.as_ptr()) };

        if !shown {
            error!("FeedDiscovery: Failed to show popup window");
            let mut state = self.popup_state.lock().unwrap();
            state.popup_window = 0;
            return Err("Failed to show popup window".into());
        }

        // Wait briefly for the popup to initialize, then navigate to auth URL
        std::thread::sleep(Duration::from_millis(800));

        {
            self.navigating.store(true, Ordering::SeqCst);
            info!("FeedDiscovery: Navigating to Microsoft login");
            let url_cstr = CString::new(auth_url.as_str()).unwrap();
            unsafe { webui_sys::webui_navigate(popup, url_cstr.as_ptr()) };
        }

        // Wait for auth code or timeout
        let state = self.popup_state.lock().unwrap();
        let (state, _timeout) = self
            .popup_cv
            .wait_timeout_while(state, Duration::from_secs(AUTH_TIMEOUT_SECS), |s| {
                !s.token_found && !s.popup_closed
            })
            .unwrap();

        let auth_code = state.auth_code.clone();
        let code_verifier = state.code_verifier.clone();
        let popup_to_close = state.popup_window;
        drop(state);

        // Close the popup
        if popup_to_close != 0 {
            info!("FeedDiscovery: Closing popup window");
            unsafe { webui_sys::webui_close(popup_to_close) };
            let mut state = self.popup_state.lock().unwrap();
            state.popup_window = 0;
        }

        if auth_code.is_empty() {
            return Err("Authentication failed or timed out".into());
        }

        // Exchange the authorization code for an access token
        info!("FeedDiscovery: Exchanging auth code for access token...");
        let token = self.exchange_code_for_token(&auth_code, &code_verifier)?;

        if token.is_empty() {
            return Err("Token exchange returned empty token".into());
        }

        info!(
            "FeedDiscovery: Authentication successful (token length={})",
            token.len()
        );
        Ok(token)
    }

    /// Exchange an authorization code for tokens via the PKCE token endpoint.
    fn exchange_code_for_token(&self, code: &str, code_verifier: &str) -> Result<String, String> {
        let post_body = format!(
            "client_id={}&code={}&redirect_uri={}&grant_type=authorization_code&code_verifier={}&scope={}",
            WVD_CLIENT_ID,
            utils::url_encode(code),
            utils::url_encode(WVD_REDIRECT_URI),
            utils::url_encode(code_verifier),
            utils::url_encode(WVD_SCOPE),
        );

        info!("FeedDiscovery: Token request to {MS_TOKEN_ENDPOINT}");

        let mut response_body = String::new();
        ureq::post(MS_TOKEN_ENDPOINT)
            .header("Content-Type", "application/x-www-form-urlencoded")
            .header("Origin", "https://client.wvd.microsoft.com")
            .send(post_body.as_bytes())
            .map_err(|e| format!("Token exchange HTTP error: {e}"))?
            .into_body()
            .read_to_string(&mut response_body)
            .map_err(|e| format!("Failed to read token response: {e}"))?;

        // Parse JSON response
        let json: serde_json::Value = serde_json::from_str(&response_body)
            .map_err(|e| format!("Token response JSON parse error: {e}"))?;

        let token = json
            .get("access_token")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string();

        if token.is_empty() {
            let err_desc = json
                .get("error_description")
                .and_then(|v| v.as_str())
                .unwrap_or("");
            let err_code = json
                .get("error")
                .and_then(|v| v.as_str())
                .unwrap_or("unknown");
            return Err(format!("Token exchange failed: {err_code} - {err_desc}"));
        }

        // Extract id_token (OIDC JWT with user claims)
        if let Some(id_token) = json.get("id_token").and_then(|v| v.as_str()) {
            if !id_token.is_empty() {
                info!(
                    "FeedDiscovery: id_token received (length={})",
                    id_token.len()
                );
                let mut state = self.popup_state.lock().unwrap();
                state.id_token = id_token.to_string();
            }
        }

        Ok(token)
    }

    // ========================================================================
    // Popup event handling
    // ========================================================================

    /// Process popup navigation events -- intercept the OAuth redirect.
    fn on_popup_navigation(&self, url: &str) {
        info!("FeedDiscovery: Navigation: {url}");

        // Track navigation to avoid premature DISCONNECTED handling
        if url.contains("login.microsoftonline.com")
            || url.contains("login.microsoft.com")
            || url.contains("login.live.com")
            || url.contains("client.wvd.microsoft.com")
            || url.contains("microsoftazuread-sso.com")
            || url.contains("fpt.dfp.microsoft.com")
        {
            self.navigating.store(true, Ordering::SeqCst);
        }

        // Detect the redirect back to the webclient carrying the auth code.
        // Microsoft returns: .../index.html#code=<AUTH_CODE>&client_info=...
        if url.contains(WVD_REDIRECT_URI) && url.contains("code=") {
            // Extract the code from URL fragment (#...) or query (?...)
            let params_str = if let Some(hash_pos) = url.find('#') {
                &url[hash_pos + 1..]
            } else if let Some(q_pos) = url.find('?') {
                &url[q_pos + 1..]
            } else {
                ""
            };

            // Find code= parameter
            let code = extract_param(params_str, "code");
            let code = utils::url_decode(&code);

            if !code.is_empty() {
                info!("FeedDiscovery: Auth code received (length={})", code.len());
                let mut state = self.popup_state.lock().unwrap();
                state.auth_code = code;
                state.token_found = true;
                self.popup_cv.notify_all();
            }
        }
    }

    /// Handle popup disconnection.
    fn on_popup_disconnected(&self) {
        info!("FeedDiscovery: Popup disconnected");

        if self.navigating.load(Ordering::SeqCst) {
            info!("FeedDiscovery: Expected disconnect during navigation, ignoring");
            return;
        }

        // User closed the popup before completing auth
        let mut state = self.popup_state.lock().unwrap();
        state.popup_closed = true;
        self.popup_cv.notify_all();
    }

    // ========================================================================
    // G3: Discover tenants (HTTP + XML)
    // ========================================================================

    /// Discover tenant feeds from the WVD feed discovery endpoint.
    fn discover_tenants(&self, bearer_token: &str) -> Result<Vec<TenantFeed>, String> {
        info!("FeedDiscovery: Fetching tenant feeds from {WVD_FEED_DISCOVERY_URL}");

        let xml = Self::http_get(WVD_FEED_DISCOVERY_URL, bearer_token)?;
        if xml.is_empty() {
            return Err("Empty response from feed discovery endpoint".into());
        }

        let mut reader = Reader::from_str(&xml);
        let mut buf = Vec::new();
        let mut tenants = Vec::new();

        loop {
            match reader.read_event_into(&mut buf) {
                Ok(Event::Start(ref e)) | Ok(Event::Empty(ref e)) => {
                    let name = local_name_str(e);
                    // Match TenantFeedURL but not TenantFeedURLs
                    if name.contains("TenantFeedURL") && !name.ends_with("TenantFeedURLs") {
                        let tenant_id = get_attr(e, b"TenantId");
                        let feed_url = utils::html_decode(&get_attr(e, b"FeedURL"));

                        if !tenant_id.is_empty() && !feed_url.is_empty() {
                            let tenant = TenantFeed {
                                tenant_id,
                                tenant_display_name: utils::html_decode(&get_attr(
                                    e,
                                    b"TenantDisplayName",
                                )),
                                workspace_name: utils::html_decode(&get_attr(e, b"WorkspaceName")),
                                feed_url,
                                geo: get_attr(e, b"Geo"),
                                arm_path: get_attr(e, b"ArmPath"),
                                hub_discovery_url: utils::html_decode(&get_attr(
                                    e,
                                    b"HubDiscoveryURL",
                                )),
                            };
                            info!(
                                "FeedDiscovery: Found tenant: {} (ID: {})",
                                tenant.workspace_name, tenant.tenant_id
                            );
                            tenants.push(tenant);
                        }
                    }
                }
                Ok(Event::Eof) => break,
                Err(e) => return Err(format!("XML parse error: {e}")),
                _ => {}
            }
            buf.clear();
        }

        info!("FeedDiscovery: Discovered {} tenants", tenants.len());
        Ok(tenants)
    }

    // ========================================================================
    // G4: Fetch tenant resources (HTTP + XML)
    // ========================================================================

    /// Fetch resources for a single tenant workspace.
    fn fetch_tenant_resources(
        &self,
        feed_url: &str,
        bearer_token: &str,
    ) -> Result<Vec<FeedResource>, String> {
        let xml = Self::http_get(feed_url, bearer_token)?;
        if xml.is_empty() {
            return Err(format!("Empty response from feed URL: {feed_url}"));
        }

        let mut reader = Reader::from_str(&xml);
        let mut buf = Vec::new();
        let mut resources = Vec::new();

        let mut current_publisher = String::new();
        let mut current_resource: Option<FeedResource> = None;
        let mut in_resource = false;

        loop {
            match reader.read_event_into(&mut buf) {
                Ok(Event::Start(ref e)) => {
                    let name = local_name_str(e);

                    if name.contains("Publisher") && !name.contains("Publishers") {
                        current_publisher = get_attr(e, b"Name");
                    } else if is_resource_element(&name) {
                        current_resource = Some(FeedResource {
                            id: get_attr(e, b"ID"),
                            title: utils::html_decode(&get_attr(e, b"Title")),
                            resource_type: get_attr(e, b"Type"),
                            arm_path: get_attr(e, b"ArmPath"),
                            publisher_name: current_publisher.clone(),
                            rdp_url: String::new(),
                            icon_url: None,
                        });
                        in_resource = true;
                    }
                }
                Ok(Event::Empty(ref e)) => {
                    let name = local_name_str(e);
                    if in_resource {
                        if name.contains("ResourceFile") {
                            if let Some(ref mut res) = current_resource {
                                res.rdp_url = utils::html_decode(&get_attr(e, b"URL"));
                            }
                        } else if name.contains("Icon32") {
                            if let Some(ref mut res) = current_resource {
                                let url = get_attr(e, b"FileURL");
                                if !url.is_empty() {
                                    res.icon_url = Some(utils::html_decode(&url));
                                }
                            }
                        }
                    }
                }
                Ok(Event::End(ref e)) => {
                    let name = end_local_name_str(e);
                    if is_resource_element(&name) && in_resource {
                        if let Some(res) = current_resource.take() {
                            if !res.id.is_empty() && !res.rdp_url.is_empty() {
                                resources.push(res);
                            }
                        }
                        in_resource = false;
                    }
                }
                Ok(Event::Eof) => break,
                Err(e) => return Err(format!("XML parse error: {e}")),
                _ => {}
            }
            buf.clear();
        }

        Ok(resources)
    }

    /// Download an RDP file and return its content.
    fn fetch_rdp_content(&self, rdp_url: &str, bearer_token: &str) -> Result<String, String> {
        Self::http_get(rdp_url, bearer_token)
    }

    // ========================================================================
    // G5: Import resource
    // ========================================================================

    /// Import a single RDP resource into the database.
    fn import_resource(
        &self,
        resource: &FeedResource,
        workspace_name: &str,
        rdp_content: &str,
        account_id: &str,
    ) -> Result<(), String> {
        if rdp_content.is_empty() {
            return Err("Empty RDP content".into());
        }

        let rdp = crate::rdp_file_parser::parse_content(rdp_content)?;
        let mut profile = rdp.to_connection_profile();

        // Override fields from the feed resource
        profile.name = resource.title.clone();
        profile.folder = format!("Feeds/{workspace_name}");
        profile.source_account_id = account_id.to_string();

        // Use feed resource arm_path if RDP didn't have one
        if profile.arm_path.is_empty() {
            profile.arm_path = resource.arm_path.clone();
        }

        // If the connection name already exists in a different folder, make it unique
        if let Ok(cm) = self.config_manager.lock() {
            if let Some(existing_json) = cm.get_connection_json(&profile.name) {
                if let Ok(existing) = serde_json::from_str::<serde_json::Value>(&existing_json) {
                    let existing_folder = existing
                        .get("folder")
                        .and_then(|v| v.as_str())
                        .unwrap_or("");
                    if existing_folder != profile.folder {
                        profile.name = format!("{workspace_name} - {}", resource.title);
                    }
                }
            }
        }

        // Ensure the folder exists
        if let Ok(cm) = self.config_manager.lock() {
            cm.create_folder(&profile.folder);
        }

        // Serialize and save
        let json = serde_json::to_string(&profile)
            .map_err(|e| format!("Failed to serialize profile: {e}"))?;

        let saved = self
            .config_manager
            .lock()
            .map_err(|e| format!("Lock error: {e}"))?
            .save_connection(&json);

        if saved {
            Ok(())
        } else {
            Err(format!("Failed to save connection: {}", profile.name))
        }
    }

    // ========================================================================
    // Progress notification
    // ========================================================================

    /// Send a progress notification to the main window.
    fn notify_progress(&self, message: &str, current: usize, total: usize) {
        if self.main_window == 0 {
            return;
        }
        let js = format!(
            r#"if (typeof onFeedDiscoveryProgress === 'function') onFeedDiscoveryProgress({{"message":"{}","current":{},"total":{}}});"#,
            json_escape(message),
            current,
            total,
        );
        run_js(self.main_window, &js);
    }

    // ========================================================================
    // HTTP helper
    // ========================================================================

    /// Perform an HTTP GET with bearer token authorization.
    fn http_get(url: &str, bearer_token: &str) -> Result<String, String> {
        let mut body = String::new();
        ureq::get(url)
            .header("Authorization", &format!("Bearer {bearer_token}"))
            .header("Accept", "application/x-msts-radc-discovery+xml,text/xml")
            .header("Origin", "https://client.wvd.microsoft.com")
            .header("Referer", WVD_REDIRECT_URI)
            .header(
                "x-ms-user-agent",
                "com.microsoft.rdc.html/2.0.69.1 rdhtml-sdk/2.0.4",
            )
            .call()
            .map_err(|e| format!("HTTP GET failed for {url}: {e}"))?
            .into_body()
            .read_to_string(&mut body)
            .map_err(|e| format!("Failed to read response from {url}: {e}"))?;

        Ok(body)
    }
}

// ============================================================================
// Static WebUI callback for popup events
// ============================================================================

fn get_feed_instance() -> Option<&'static FeedDiscoveryManager> {
    FEED_INSTANCE.get().map(|ptr| unsafe { &**ptr })
}

/// Static event handler for the OAuth popup window.
unsafe extern "C" fn s_handle_popup_events(e: *mut webui_sys::webui_event_t) {
    let Some(instance) = get_feed_instance() else {
        return;
    };
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
            instance.on_popup_navigation(&url);
        }
    } else if event_type == DISCONNECTED {
        instance.on_popup_disconnected();
    }
}

// ============================================================================
// XML parsing helpers
// ============================================================================

/// Extract the local name of an element from a `BytesStart`.
fn local_name_str(e: &quick_xml::events::BytesStart) -> String {
    String::from_utf8_lossy(e.local_name().as_ref()).to_string()
}

/// Extract the local name from a `BytesEnd`.
fn end_local_name_str(e: &quick_xml::events::BytesEnd) -> String {
    String::from_utf8_lossy(e.local_name().as_ref()).to_string()
}

/// Extract an attribute value by local name from a `BytesStart`.
fn get_attr(e: &quick_xml::events::BytesStart, name: &[u8]) -> String {
    e.attributes()
        .filter_map(|a| a.ok())
        .find(|a| a.key.local_name().as_ref() == name)
        .map(|a| String::from_utf8_lossy(&a.value).to_string())
        .unwrap_or_default()
}

/// Check if an element name represents a `Resource` element
/// (not `Resources`, `ResourceFile`, or `ResourceCollection`).
fn is_resource_element(name: &str) -> bool {
    name == "Resource" || (name.ends_with(":Resource") && !name.contains("Resources"))
}

/// Extract a parameter value from a URL query/fragment string.
fn extract_param(params: &str, key: &str) -> String {
    let prefix = format!("{key}=");
    for part in params.split('&') {
        if let Some(value) = part.strip_prefix(&prefix) {
            return value.to_string();
        }
    }
    String::new()
}

/// Escape a string for embedding inside a JSON string literal.
fn json_escape(s: &str) -> String {
    s.replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('\n', "\\n")
        .replace('\r', "\\r")
}

/// Execute JavaScript on a WebUI window (fire-and-forget).
fn run_js(window: usize, js: &str) {
    if let Ok(cstr) = CString::new(js) {
        unsafe { webui_sys::webui_run(window, cstr.as_ptr()) };
    }
}

// ============================================================================
// Placeholder HTML for the auth popup
// ============================================================================

const PLACEHOLDER_HTML: &str = r#"<!DOCTYPE html>
<html>
<head>
    <title>WVD Feed Discovery - Login</title>
    <script src="webui.js"></script>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            display: flex; flex-direction: column; align-items: center;
            justify-content: center; height: 100vh; margin: 0;
            background: #1a1a2e; color: #e0e0e0;
        }
        h1 { color: #a0a0ff; margin-bottom: 20px; }
        .spinner {
            width: 50px; height: 50px;
            border: 5px solid #333; border-top: 5px solid #0078d4;
            border-radius: 50%; animation: spin 1s linear infinite;
        }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
        p { color: #888; margin-top: 20px; }
    </style>
</head>
<body>
    <h1>WVD Feed Discovery</h1>
    <div class="spinner"></div>
    <p>Redirecting to Microsoft login...</p>
</body>
</html>"#;
