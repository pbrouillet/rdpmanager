/**
 * Connection Types - Shared data structures for RDP connections
 *
 * Defines the core types used across the codebase for certificate
 * verification, authentication, and AAD OAuth flows.
 *
 * These types were previously defined in rdp_launcher.hpp but are used by
 * DialogManager, AADAuthHandler, JSHandlers, and FeedDiscoveryManager.
 * Moving them here breaks the false coupling that forced every consumer
 * to pull in all of rdp_launcher.hpp.
 */

#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <vector>

// ============================================================================
// Folder settings for parameter inheritance
// ============================================================================

/**
 * Sparse settings that can be attached to a folder.
 * Only fields with a value set will override the inherited defaults.
 * Fields left as std::nullopt are inherited from the parent folder chain.
 */
struct FolderSettings {
    // Features
    std::optional<bool> home_drive;
    std::optional<bool> clipboard;
    std::optional<bool> cert_tofu;
    std::optional<bool> usb_auto;
    std::optional<bool> floatbar;

    // Performance
    std::optional<bool> dynamic_resolution;
    std::optional<bool> network_auto;
    std::optional<bool> gfx_avc420;
    std::optional<bool> compression;

    // Audio & Session
    std::optional<bool> audio_pulse;
    std::optional<bool> prevent_session_lock;

    // Security & Reconnection
    std::optional<bool> auto_reconnect;
    std::optional<int> auto_reconnect_max_retries;

    // Gateway
    std::optional<std::string> gateway_hostname;
    std::optional<bool> enable_rds_aad_auth;
    std::optional<bool> target_is_aad_joined;
    std::optional<std::string> load_balance_info;
};

// List of all inheritable field names (used for backward compat and validation)
inline const std::vector<std::string>& inheritable_field_names() {
    static const std::vector<std::string> names = {
        "home_drive", "clipboard", "cert_tofu", "usb_auto", "floatbar",
        "dynamic_resolution", "network_auto", "gfx_avc420", "compression",
        "audio_pulse", "prevent_session_lock",
        "auto_reconnect", "auto_reconnect_max_retries",
        "gateway_hostname", "enable_rds_aad_auth", "target_is_aad_joined",
        "load_balance_info"
    };
    return names;
}

// ============================================================================
// Certificate verification types
// ============================================================================

/**
 * Certificate information for verification dialogs
 */
struct CertificateInfo {
    std::string host;
    uint16_t port;
    std::string common_name;
    std::string subject;
    std::string issuer;
    std::string fingerprint;
    bool is_changed;           // true if certificate changed from stored one
    std::string old_fingerprint;  // Previous fingerprint if changed
};

/**
 * Certificate verification result
 */
enum class CertificateAcceptance {
    Reject = 0,              // Don't accept the certificate
    AcceptPermanently = 1,   // Accept and store for future connections
    AcceptTemporarily = 2    // Accept for this session only
};

// ============================================================================
// Authentication request / response types
// ============================================================================

/**
 * Authentication request information
 */
struct AuthRequest {
    std::string hostname;
    bool is_gateway;         // true if this is for gateway authentication
    std::string current_username;
    std::string current_domain;
};

/**
 * Authentication response
 */
struct AuthResponse {
    bool success;
    std::string username;
    std::string password;
    std::string domain;
};

// ============================================================================
// Azure AD authentication types
// ============================================================================

/**
 * AAD Authentication request - for OAuth2 code flow
 */
struct AADAuthRequest {
    enum class Type {
        RDS_AAD,        // RDS AAD authentication (ACCESS_TOKEN_TYPE_AAD)
        AVD             // Azure Virtual Desktop (ACCESS_TOKEN_TYPE_AVD)
    };
    Type type;
    std::string auth_url;      // URL to present to user for login
    std::string scope;         // OAuth scope (for RDS_AAD)
    std::string req_cnf;       // Request confirmation (for RDS_AAD)
};

/**
 * AAD Authentication response
 */
struct AADAuthResponse {
    bool success;
    std::string redirect_url;  // The redirect URL containing the authorization code
    std::string actual_redirect_uri;  // The redirect_uri actually used in the auth request (localhost)
};

// ============================================================================
// Callback type aliases
// ============================================================================

using CertificateVerifyCallback = std::function<CertificateAcceptance(const CertificateInfo& info)>;
using AuthenticateCallback = std::function<AuthResponse(const AuthRequest& request)>;
using AADAuthCallback = std::function<AADAuthResponse(const AADAuthRequest& request)>;

using TokenCacheLookupCallback =
    std::function<std::optional<std::string>(const std::string& hostname, const std::string& cache_kind)>;
using TokenCacheStoreCallback =
    std::function<bool(const std::string& hostname,
                       const std::string& cache_kind,
                       const std::string& token,
                       int64_t expires_at_epoch)>;


