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


