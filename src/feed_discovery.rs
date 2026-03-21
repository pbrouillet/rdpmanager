//! WVD/AVD feed discovery and import.
//!
//! Authenticates via OAuth2 PKCE flow, discovers Azure Virtual Desktop
//! tenants and resources, downloads RDP files, and imports them as
//! connection profiles.
//!
//! Flow: authenticate_popup() → discover_tenants() → fetch_resources() →
//!       fetch_rdp_content() → import_resource()
//!
//! Equivalent to: `src/feed_discovery.cpp` / `feed_discovery.hpp`

// TODO: Implement FeedDiscoveryManager with:
// - OAuth2 PKCE authentication (code_verifier, code_challenge, SHA-256)
// - WebUI popup window for Microsoft login
// - HTTP client (ureq) for WVD API calls with Bearer auth
// - XML parsing (quick-xml) for tenant/resource discovery feeds
// - RDP file content download and parsing
// - Connection profile import into ConfigManager
// - Progress notifications to UI via webui_run()
// - Constants: WVD_CLIENT_ID, WVD_REDIRECT_URI, MS endpoints
