//! .rdp file import and parsing.
//!
//! Parses RDP file content (INI-like `key:type:value` format) into
//! [`RdpFileData`] and then maps it to [`ConnectionProfile`].
//! Used for manual RDP file import and WVD feed resource import.
//!
//! Equivalent to: `src/rdp_file_parser.cpp` / `rdp_file_parser.hpp`

use std::path::Path;

use crate::types::ConnectionProfile;

// ---------------------------------------------------------------------------
// Parsed intermediate representation
// ---------------------------------------------------------------------------

/// All supported .rdp / .rdpw file properties, mirroring the C++ `RDPFileData`.
#[derive(Debug, Clone)]
pub struct RdpFileData {
    // Basic connection
    pub full_address: String,
    pub alternate_full_address: String,
    pub server_port: i32,

    // Authentication
    pub username: String,
    pub domain: String,
    pub prompt_for_credentials: bool,
    pub prompt_credential_once: bool,
    pub authentication_level: i32,

    // Azure AD
    pub target_is_aad_joined: bool,
    pub enable_rds_aad_auth: bool,
    pub aad_tenant_id: String,

    // RD Gateway
    pub gateway_hostname: String,
    pub gateway_usage_method: i32,
    pub gateway_profile_usage_method: i32,
    pub gateway_credentials_source: i32,
    pub gateway_brokering_type: i32,

    // AVD / WVD
    pub load_balance_info: String,
    pub wvd_endpoint_pool: String,
    pub arm_path: String,
    pub workspace_id: String,
    pub resource_provider: String,
    pub geo: String,
    pub diagnostic_service_url: String,
    pub hub_discovery_geo_url: String,
    pub activity_hint: String,

    // Remote Application
    pub remote_application_program: String,
    pub remote_desktop_name: String,
    pub remote_application_mode: i32,

    // Display
    pub desktop_width: i32,
    pub desktop_height: i32,
    pub screen_mode_id: i32,
    pub smart_sizing: bool,
    pub dynamic_resolution: bool,
    pub single_mon_in_windowed_mode: i32,

    // Redirection
    pub redirect_clipboard: bool,
    pub redirect_printers: bool,
    pub redirect_smart_cards: bool,
    pub redirect_com_ports: bool,
    pub redirect_location: bool,
    pub drives_to_redirect: String,
    pub devices_to_redirect: String,
    pub cameras_to_redirect: String,
    pub usb_devices_to_redirect: String,

    // Audio
    pub audio_mode: i32,
    pub audio_capture_mode: i32,

    // Security / signature
    pub sign_scope: String,
    pub signature: String,
}

impl Default for RdpFileData {
    fn default() -> Self {
        Self {
            full_address: String::new(),
            alternate_full_address: String::new(),
            server_port: 3389,

            username: String::new(),
            domain: String::new(),
            prompt_for_credentials: true,
            prompt_credential_once: false,
            authentication_level: 2,

            target_is_aad_joined: false,
            enable_rds_aad_auth: false,
            aad_tenant_id: String::new(),

            gateway_hostname: String::new(),
            gateway_usage_method: 0,
            gateway_profile_usage_method: 0,
            gateway_credentials_source: 0,
            gateway_brokering_type: 0,

            load_balance_info: String::new(),
            wvd_endpoint_pool: String::new(),
            arm_path: String::new(),
            workspace_id: String::new(),
            resource_provider: String::new(),
            geo: String::new(),
            diagnostic_service_url: String::new(),
            hub_discovery_geo_url: String::new(),
            activity_hint: String::new(),

            remote_application_program: String::new(),
            remote_desktop_name: String::new(),
            remote_application_mode: 0,

            desktop_width: 0,
            desktop_height: 0,
            screen_mode_id: 1,
            smart_sizing: false,
            dynamic_resolution: false,
            single_mon_in_windowed_mode: 0,

            redirect_clipboard: true,
            redirect_printers: false,
            redirect_smart_cards: false,
            redirect_com_ports: false,
            redirect_location: false,
            drives_to_redirect: String::new(),
            devices_to_redirect: String::new(),
            cameras_to_redirect: String::new(),
            usb_devices_to_redirect: String::new(),

            audio_mode: 0,
            audio_capture_mode: 0,

            sign_scope: String::new(),
            signature: String::new(),
        }
    }
}

// ---------------------------------------------------------------------------
// Helper methods on RdpFileData
// ---------------------------------------------------------------------------

impl RdpFileData {
    /// True when the file describes an Azure Virtual Desktop / WVD session.
    pub fn is_avd_connection(&self) -> bool {
        !self.wvd_endpoint_pool.is_empty()
            || !self.arm_path.is_empty()
            || !self.load_balance_info.is_empty()
            || self.enable_rds_aad_auth
    }

    /// True when an RD Gateway is configured and enabled.
    pub fn uses_gateway(&self) -> bool {
        !self.gateway_hostname.is_empty() && self.gateway_usage_method > 0
    }

    /// True when Azure AD authentication is required.
    pub fn requires_aad_auth(&self) -> bool {
        self.target_is_aad_joined || self.enable_rds_aad_auth
    }

    /// Effective connection host (gateway for AVD, full_address otherwise).
    pub fn get_connection_host(&self) -> &str {
        if self.is_avd_connection() && self.uses_gateway() {
            return &self.gateway_hostname;
        }
        // Strip an embedded `:port` suffix if present.
        self.full_address
            .rsplit_once(':')
            .map_or(self.full_address.as_str(), |(host, _)| host)
    }

    /// Display name for the UI (remote desktop name or hostname).
    pub fn get_display_name(&self) -> &str {
        if !self.remote_desktop_name.is_empty() {
            return &self.remote_desktop_name;
        }
        &self.full_address
    }

    /// Convert the parsed data into a [`ConnectionProfile`].
    pub fn to_connection_profile(&self) -> ConnectionProfile {
        // Separate host and optional embedded port from "full address".
        let (hostname, embedded_port) = split_host_port(&self.full_address);

        let port = if self.server_port != 3389 && self.server_port > 0 {
            self.server_port as u16
        } else if let Some(p) = embedded_port {
            p
        } else {
            3389
        };

        ConnectionProfile {
            name: self.get_display_name().to_owned(),
            hostname: hostname.to_owned(),
            port,
            username: self.username.clone(),
            domain: self.domain.clone(),
            width: if self.desktop_width > 0 {
                self.desktop_width as u32
            } else {
                1920
            },
            height: if self.desktop_height > 0 {
                self.desktop_height as u32
            } else {
                1080
            },
            fullscreen: self.screen_mode_id == 2,
            clipboard: self.redirect_clipboard,
            home_drive: !self.drives_to_redirect.is_empty(),
            dynamic_resolution: self.dynamic_resolution,
            gateway_hostname: self.gateway_hostname.clone(),
            enable_rds_aad_auth: self.enable_rds_aad_auth,
            target_is_aad_joined: self.target_is_aad_joined,
            load_balance_info: self.load_balance_info.clone(),
            aad_tenant_id: self.aad_tenant_id.clone(),
            wvd_endpoint_pool: self.wvd_endpoint_pool.clone(),
            workspace_id: self.workspace_id.clone(),
            arm_path: self.arm_path.clone(),
            remote_application_program: self.remote_application_program.clone(),
            remote_desktop_name: self.remote_desktop_name.clone(),
            ..ConnectionProfile::default()
        }
    }
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

/// Parse the text content of an `.rdp` file into [`RdpFileData`].
///
/// Lines that are empty, start with `#` or `;`, or do not match the
/// `key:type:value` format are silently skipped.
pub fn parse_content(content: &str) -> Result<RdpFileData, String> {
    let mut data = RdpFileData::default();

    for line in content.lines() {
        let line = line.trim_end_matches('\r');
        if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
            continue;
        }

        let (key, _type_ch, value) = match parse_line(line) {
            Some(t) => t,
            None => continue,
        };

        let key_lower = key.to_ascii_lowercase();
        apply_field(&mut data, &key_lower, value);
    }

    if data.full_address.is_empty() && data.gateway_hostname.is_empty() {
        return Err("No valid connection address found".into());
    }

    Ok(data)
}

/// Read an `.rdp` file from disk, strip a UTF-8 BOM if present, and parse.
pub fn parse_file(path: &Path) -> Result<RdpFileData, String> {
    let bytes = std::fs::read(path).map_err(|e| format!("Failed to open file: {e}"))?;

    // Strip UTF-8 BOM (EF BB BF) if present.
    let content = if bytes.starts_with(&[0xEF, 0xBB, 0xBF]) {
        std::str::from_utf8(&bytes[3..])
    } else {
        std::str::from_utf8(&bytes)
    }
    .map_err(|e| format!("Invalid UTF-8: {e}"))?;

    parse_content(content)
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Split a single RDP line into `(key, type_char, value)`.
fn parse_line(line: &str) -> Option<(&str, char, &str)> {
    let first = line.find(':')?;
    let rest = &line[first + 1..];
    let second = rest.find(':')?;

    let key = &line[..first];
    let type_str = &rest[..second];
    let type_ch = type_str.chars().next()?;
    let value = &rest[second + 1..];

    Some((key, type_ch, value))
}

/// Apply a parsed key/value pair to the correct field on [`RdpFileData`].
fn apply_field(data: &mut RdpFileData, key: &str, value: &str) {
    // Helpers
    let as_bool = || value == "1";
    let as_int = || value.parse::<i32>().unwrap_or(0);

    match key {
        // Basic connection
        "full address" => data.full_address = value.to_owned(),
        "alternate full address" => data.alternate_full_address = value.to_owned(),
        "server port" => data.server_port = as_int(),
        "username" => data.username = value.to_owned(),
        "domain" => data.domain = value.to_owned(),

        // Authentication
        "prompt for credentials" => data.prompt_for_credentials = as_bool(),
        "promptcredentialonce" => data.prompt_credential_once = as_bool(),
        "authentication level" => data.authentication_level = as_int(),

        // Azure AD
        "targetisaadjoined" => data.target_is_aad_joined = as_bool(),
        "enablerdsaadauth" => data.enable_rds_aad_auth = as_bool(),
        "aadtenantid" => data.aad_tenant_id = value.to_owned(),

        // Gateway
        "gatewayhostname" => data.gateway_hostname = value.to_owned(),
        "gatewayusagemethod" => data.gateway_usage_method = as_int(),
        "gatewayprofileusagemethod" => data.gateway_profile_usage_method = as_int(),
        "gatewaycredentialssource" => data.gateway_credentials_source = as_int(),
        "gatewaybrokeringtype" => data.gateway_brokering_type = as_int(),

        // AVD / WVD
        "loadbalanceinfo" => data.load_balance_info = value.to_owned(),
        "wvd endpoint pool" => data.wvd_endpoint_pool = value.to_owned(),
        "armpath" => data.arm_path = value.to_owned(),
        "workspace id" => data.workspace_id = value.to_owned(),
        "resourceprovider" => data.resource_provider = value.to_owned(),
        "geo" => data.geo = value.to_owned(),
        "diagnosticserviceurl" => data.diagnostic_service_url = value.to_owned(),
        "hubdiscoverygeourl" => data.hub_discovery_geo_url = value.to_owned(),
        "activityhint" => data.activity_hint = value.to_owned(),

        // Remote Application
        "remoteapplicationprogram" => data.remote_application_program = value.to_owned(),
        "remotedesktopname" => data.remote_desktop_name = value.to_owned(),
        "remoteapplicationmode" => data.remote_application_mode = as_int(),

        // Display
        "desktopwidth" => data.desktop_width = as_int(),
        "desktopheight" => data.desktop_height = as_int(),
        "screen mode id" => data.screen_mode_id = as_int(),
        "smart sizing" => data.smart_sizing = as_bool(),
        "dynamic resolution" => data.dynamic_resolution = as_bool(),
        "singlemoninwindowedmode" => data.single_mon_in_windowed_mode = as_int(),

        // Redirection
        "redirectclipboard" => data.redirect_clipboard = as_bool(),
        "redirectprinters" => data.redirect_printers = as_bool(),
        "redirectsmartcards" => data.redirect_smart_cards = as_bool(),
        "redirectcomports" => data.redirect_com_ports = as_bool(),
        "redirectlocation" => data.redirect_location = as_bool(),
        "drivestoredirect" => data.drives_to_redirect = value.to_owned(),
        "devicestoredirect" => data.devices_to_redirect = value.to_owned(),
        "camerastoredirect" => data.cameras_to_redirect = value.to_owned(),
        "usbdevicestoredirect" => data.usb_devices_to_redirect = value.to_owned(),

        // Audio
        "audiomode" => data.audio_mode = as_int(),
        "audiocapturemode" => data.audio_capture_mode = as_int(),

        // Security
        "signscope" => data.sign_scope = value.to_owned(),
        "signature" => data.signature = value.to_owned(),

        _ => {} // Unknown keys are silently ignored.
    }
}

/// Split `"host:port"` into `(host, Some(port))` or `("host", None)`.
fn split_host_port(address: &str) -> (&str, Option<u16>) {
    if let Some((host, port_str)) = address.rsplit_once(':') {
        if let Ok(port) = port_str.parse::<u16>() {
            return (host, Some(port));
        }
    }
    (address, None)
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_basic_rdp() {
        let content = "\
full address:s:server.example.com
server port:i:3390
username:s:user@domain.com
domain:s:CORP
desktopwidth:i:1920
desktopheight:i:1080
screen mode id:i:2
redirectclipboard:i:1
";
        let data = parse_content(content).unwrap();
        assert_eq!(data.full_address, "server.example.com");
        assert_eq!(data.server_port, 3390);
        assert_eq!(data.username, "user@domain.com");
        assert_eq!(data.domain, "CORP");
        assert_eq!(data.desktop_width, 1920);
        assert_eq!(data.desktop_height, 1080);
        assert_eq!(data.screen_mode_id, 2);
        assert!(data.redirect_clipboard);
    }

    #[test]
    fn parse_avd_rdp() {
        let content = "\
full address:s:rdgateway.wvd.microsoft.com
gatewayhostname:s:rdgateway.wvd.microsoft.com
gatewayusagemethod:i:1
loadbalanceinfo:s:tsv://MS Terminal Services Plugin.1.wvd
targetisaadjoined:i:1
enablerdsaadauth:i:1
workspace id:s:ws-123
";
        let data = parse_content(content).unwrap();
        assert!(data.is_avd_connection());
        assert!(data.uses_gateway());
        assert!(data.requires_aad_auth());
        assert_eq!(data.get_connection_host(), "rdgateway.wvd.microsoft.com");
    }

    #[test]
    fn skips_comments_and_empty_lines() {
        let content = "\
# comment
; another comment

full address:s:host.test
";
        let data = parse_content(content).unwrap();
        assert_eq!(data.full_address, "host.test");
    }

    #[test]
    fn rejects_missing_address() {
        let content = "username:s:foo\n";
        assert!(parse_content(content).is_err());
    }

    #[test]
    fn to_connection_profile_basic() {
        let content = "\
full address:s:myhost.com:3390
screen mode id:i:2
drivestoredirect:s:*
redirectclipboard:i:1
";
        let data = parse_content(content).unwrap();
        let prof = data.to_connection_profile();
        assert_eq!(prof.hostname, "myhost.com");
        assert_eq!(prof.port, 3390);
        assert!(prof.fullscreen);
        assert!(prof.home_drive);
        assert!(prof.clipboard);
    }

    #[test]
    fn display_name_prefers_remote_desktop_name() {
        let content = "\
full address:s:host.test
remotedesktopname:s:My Remote Desktop
";
        let data = parse_content(content).unwrap();
        assert_eq!(data.get_display_name(), "My Remote Desktop");
    }

    #[test]
    fn display_name_falls_back_to_address() {
        let content = "full address:s:fallback.host\n";
        let data = parse_content(content).unwrap();
        assert_eq!(data.get_display_name(), "fallback.host");
    }

    #[test]
    fn gateway_only_file_accepted() {
        let content = "\
gatewayhostname:s:gw.example.com
gatewayusagemethod:i:1
";
        let data = parse_content(content).unwrap();
        assert!(data.uses_gateway());
    }

    #[test]
    fn embedded_port_in_full_address() {
        let content = "full address:s:server.com:5555\n";
        let data = parse_content(content).unwrap();
        let prof = data.to_connection_profile();
        assert_eq!(prof.hostname, "server.com");
        assert_eq!(prof.port, 5555);
    }

    #[test]
    fn server_port_overrides_embedded() {
        let content = "\
full address:s:server.com:5555
server port:i:6666
";
        let data = parse_content(content).unwrap();
        let prof = data.to_connection_profile();
        assert_eq!(prof.port, 6666);
    }
}
