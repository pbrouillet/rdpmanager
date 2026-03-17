/**
 * RDP Launcher Implementation
 * 
 * Uses FreeRDP library API to create RDP client connections.
 * Directly links to the X11 FreeRDP client for popup windows.
 */

#include "rdp_launcher.hpp"
#include "utils.hpp"
#include "logger.hpp"

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdarg>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <cctype>
#include <format>

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <signal.h>
#endif

// FreeRDP library headers for direct integration
#include <freerdp/freerdp.h>
#include <freerdp/client.h>
#include <freerdp/settings.h>
#include <freerdp/version.h>
#include <freerdp/utils/aad.h>

// For UINT16, DWORD, BOOL types
#include <winpr/wtypes.h>
#include <winpr/ssl.h>

// For storing session associations with freerdp instances
#include <unordered_map>

// X11 client entry point
extern "C" {
    int RdpClientEntry(RDP_CLIENT_ENTRY_POINTS* pEntryPoints);
}

namespace fs = std::filesystem;

// Global map to associate freerdp instances with RDPSession objects
// This is needed because FreeRDP callbacks don't provide a custom user data pointer
static std::mutex g_session_map_mutex;
static std::unordered_map<freerdp*, RDPSession*> g_session_map;

namespace {

std::string normalize_host_key(std::string host) {
    if (host.empty()) {
        return host;
    }

    const size_t scheme_pos = host.find("://");
    if (scheme_pos != std::string::npos) {
        host = host.substr(scheme_pos + 3);
    }

    const size_t slash_pos = host.find('/');
    if (slash_pos != std::string::npos) {
        host = host.substr(0, slash_pos);
    }

    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return host;
}

} // namespace

// ============================================================================
// RDPSession Implementation
// ============================================================================

RDPSession::RDPSession(const RDPConnectionParams& params)
    : m_params(params)
    , m_state(RDPConnectionState::Disconnected)
    , m_running(false)
    , m_pid(-1)
{
}

RDPSession::~RDPSession() {
    stop();
}

void RDPSession::set_certificate_callback(CertificateVerifyCallback callback) {
    m_cert_callback = std::move(callback);
}

void RDPSession::set_authenticate_callback(AuthenticateCallback callback) {
    m_auth_callback = std::move(callback);
}

void RDPSession::set_aad_auth_callback(AADAuthCallback callback) {
    m_aad_callback = std::move(callback);
}

void RDPSession::set_token_cache_lookup_callback(TokenCacheLookupCallback callback) {
    m_token_cache_lookup_callback = std::move(callback);
}

void RDPSession::set_token_cache_store_callback(TokenCacheStoreCallback callback) {
    m_token_cache_store_callback = std::move(callback);
}

// Static callback trampolines - these extract the RDPSession from the global map
uint32_t RDPSession::verify_certificate_callback(freerdp* instance, const char* host, uint16_t port,
                                               const char* common_name, const char* subject,
                                               const char* issuer, const char* fingerprint, uint32_t flags) {
    (void)flags;  // Unused
    if (!instance) return 0;
    
    // Look up the session from the global map
    RDPSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_session_map_mutex);
        auto it = g_session_map.find(instance);
        if (it != g_session_map.end()) {
            session = it->second;
        }
    }
    
    if (!session || !session->m_cert_callback) {
        // No callback set - prompt in console (default behavior)
        LOG_WARN("RDPSession", "Certificate verification required (no UI callback set)");
        LOG_WARN("RDPSession", "  Host: " << (host ? host : "") << ":" << port);
        LOG_WARN("RDPSession", "  CN: " << (common_name ? common_name : ""));
        LOG_WARN("RDPSession", "  Fingerprint: " << (fingerprint ? fingerprint : ""));
        // Return 0 to reject, as we can't show a dialog
        return 0;
    }
    
    CertificateInfo info{
        .host = host ? host : "",
        .port = port,
        .common_name = common_name ? common_name : "",
        .subject = subject ? subject : "",
        .issuer = issuer ? issuer : "",
        .fingerprint = fingerprint ? fingerprint : "",
        .is_changed = false,
        .old_fingerprint = {},
    };
    
    auto result = session->m_cert_callback(info);
    return static_cast<uint32_t>(result);
}

uint32_t RDPSession::verify_changed_certificate_callback(freerdp* instance, const char* host, uint16_t port,
                                                       const char* common_name, const char* subject,
                                                       const char* issuer, const char* new_fingerprint,
                                                       const char* old_subject, const char* old_issuer,
                                                       const char* old_fingerprint, uint32_t flags) {
    (void)old_subject;  // Unused
    (void)old_issuer;   // Unused
    (void)flags;        // Unused
    
    if (!instance) return 0;
    
    RDPSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_session_map_mutex);
        auto it = g_session_map.find(instance);
        if (it != g_session_map.end()) {
            session = it->second;
        }
    }
    
    if (!session || !session->m_cert_callback) {
        LOG_INFO("RDPSession", "Changed certificate verification required (no UI callback set)");
        return 0;
    }
    
    CertificateInfo info{
        .host = host ? host : "",
        .port = port,
        .common_name = common_name ? common_name : "",
        .subject = subject ? subject : "",
        .issuer = issuer ? issuer : "",
        .fingerprint = new_fingerprint ? new_fingerprint : "",
        .is_changed = true,
        .old_fingerprint = old_fingerprint ? old_fingerprint : "",
    };
    
    auto result = session->m_cert_callback(info);
    return static_cast<uint32_t>(result);
}

int RDPSession::authenticate_callback(freerdp* instance, char** username, char** password, char** domain) {
    if (!instance) return 0;
    
    RDPSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_session_map_mutex);
        auto it = g_session_map.find(instance);
        if (it != g_session_map.end()) {
            session = it->second;
        }
    }
    
    if (!session || !session->m_auth_callback) {
        LOG_INFO("RDPSession", "Authentication required (no UI callback set)");
        return 0;
    }
    
    AuthRequest request{
        .hostname = (instance->context && instance->context->settings)
            ? freerdp_settings_get_string(instance->context->settings, FreeRDP_ServerHostname)
            : "",
        .is_gateway = false,
        .current_username = (username && *username) ? *username : "",
        .current_domain = (domain && *domain) ? *domain : "",
    };
    
    AuthResponse response = session->m_auth_callback(request);
    
    if (!response.success) {
        return 0;
    }
    
    // Free old values and set new ones
    if (username) {
        free(*username);
        *username = strdup(response.username.c_str());
    }
    if (password) {
        free(*password);
        *password = strdup(response.password.c_str());
    }
    if (domain) {
        free(*domain);
        *domain = strdup(response.domain.c_str());
    }
    
    return 1;
}

int RDPSession::gateway_authenticate_callback(freerdp* instance, char** username, char** password, char** domain) {
    if (!instance) return 0;
    
    RDPSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_session_map_mutex);
        auto it = g_session_map.find(instance);
        if (it != g_session_map.end()) {
            session = it->second;
        }
    }
    
    if (!session || !session->m_auth_callback) {
        LOG_INFO("RDPSession", "Gateway authentication required (no UI callback set)");
        return 0;
    }
    
    AuthRequest request{
        .hostname = (instance->context && instance->context->settings)
            ? freerdp_settings_get_string(instance->context->settings, FreeRDP_GatewayHostname)
            : "",
        .is_gateway = true,
        .current_username = (username && *username) ? *username : "",
        .current_domain = (domain && *domain) ? *domain : "",
    };
    
    auto response = session->m_auth_callback(request);
    
    if (!response.success) {
        return 0;
    }
    
    if (username) {
        free(*username);
        *username = strdup(response.username.c_str());
    }
    if (password) {
        free(*password);
        *password = strdup(response.password.c_str());
    }
    if (domain) {
        free(*domain);
        *domain = strdup(response.domain.c_str());
    }
    
    return 1;
}

std::string RDPSession::extract_code_from_url(const std::string& url) {
    // Find "code=" in the URL
    size_t code_pos = url.find("code=");
    if (code_pos == std::string::npos) {
        return "";
    }
    
    code_pos += 5;  // Skip "code="
    
    // Find the end of the code (either '&' or end of string)
    size_t end_pos = url.find('&', code_pos);
    if (end_pos == std::string::npos) {
        end_pos = url.length();
    }
    
    std::string code = url.substr(code_pos, end_pos - code_pos);
    return code;
}

/**
 * Replace ms-appx-web:// redirect URI with nativeclient redirect URI in OAuth URLs.
 * FreeRDP generates URLs with ms-appx-web:// which is Windows-only.
 * We replace it with the standard nativeclient redirect URI for cross-platform support.
 * 
 * Expected output format:
 * https://login.microsoftonline.com/common/oauth2/v2.0/authorize?client_id=...&response_type=code&scope=...&redirect_uri=https%3A%2F%2Flogin.microsoftonline.com%2Fcommon%2Foauth2%2Fnativeclient
 */
std::string RDPSession::replace_msappx_redirect_uri(const std::string& url) {
    // Look for ms-appx-web redirect URI (URL encoded or not)
    // Pattern: redirect_uri=ms-appx-web%3a%2f%2fMicrosoft.AAD.BrokerPlugin%2f<client_id>
    // or: redirect_uri=ms-appx-web://Microsoft.AAD.BrokerPlugin/<client_id>
    
    const std::string nativeclient_redirect = "https%3A%2F%2Flogin.microsoftonline.com%2Fcommon%2Foauth2%2Fnativeclient";
    
    // Find redirect_uri parameter (case-insensitive search for the parameter name)
    size_t redirect_pos = url.find("redirect_uri=");
    if (redirect_pos == std::string::npos) {
        redirect_pos = url.find("Redirect_uri=");
    }
    if (redirect_pos == std::string::npos) {
        redirect_pos = url.find("REDIRECT_URI=");
    }
    if (redirect_pos == std::string::npos) {
        LOG_DEBUG("RDPSession", "replace_msappx_redirect_uri: No redirect_uri parameter found");
        return url;  // No redirect_uri found
    }
    
    // Check if it's an ms-appx-web redirect
    size_t value_start = redirect_pos + 13;  // Length of "redirect_uri="
    std::string remaining = url.substr(value_start);
    
    LOG_DEBUG("RDPSession", "replace_msappx_redirect_uri: Found redirect_uri value starting with: " 
              << remaining.substr(0, 30) << "...");
    
    // Check for URL-encoded ms-appx-web (various case combinations)
    // ms-appx-web%3a, ms-appx-web%3A, MS-APPX-WEB%3a, etc.
    std::string remaining_lower = remaining;
    std::transform(remaining_lower.begin(), remaining_lower.end(), remaining_lower.begin(), ::tolower);
    
    bool is_msappx = (remaining_lower.find("ms-appx-web%3a") == 0 || 
                      remaining_lower.find("ms-appx-web://") == 0 ||
                      remaining_lower.find("ms-appx-web:") == 0);
    
    if (!is_msappx) {
        LOG_DEBUG("RDPSession", "replace_msappx_redirect_uri: Not an ms-appx-web redirect, skipping");
        return url;  // Not an ms-appx-web redirect
    }
    
    LOG_DEBUG("RDPSession", "replace_msappx_redirect_uri: Detected ms-appx-web redirect, replacing...");
    
    // Find the end of the redirect_uri value (next & or end of string)
    size_t value_end = remaining.find('&');
    if (value_end == std::string::npos) {
        value_end = remaining.length();
    }
    
    // Replace the redirect_uri value
    std::string result = url.substr(0, value_start) + nativeclient_redirect;
    if (value_end < remaining.length()) {
        result += remaining.substr(value_end);  // Append remaining parameters
    }
    
    LOG_DEBUG("RDPSession", "replace_msappx_redirect_uri: Replacement complete");
    
    return result;
}

/**
 * Replace the redirect_uri in a token request body with the actual URI used during auth.
 * The token request is a form-encoded body (key=value&key=value).
 */
static std::string replace_redirect_uri_in_request(const std::string& request, const std::string& actual_uri) {
    if (actual_uri.empty()) {
        LOG_INFO("RDPSession", "replace_redirect_uri: no actual_redirect_uri, returning as-is");
        return request;
    }

    size_t pos = request.find("redirect_uri=");
    if (pos == std::string::npos) {
        LOG_INFO("RDPSession", "replace_redirect_uri: no redirect_uri param found");
        return request;
    }

    size_t value_start = pos + 13; // length of "redirect_uri="
    size_t value_end = request.find('&', value_start);
    if (value_end == std::string::npos) {
        value_end = request.length();
    }

    // URL-encode the actual redirect URI for the form body
    std::string encoded_uri;
    for (auto c : actual_uri) {
        auto uc = static_cast<unsigned char>(c);
        if (isalnum(uc) || uc == '-' || uc == '_' || uc == '.' || uc == '~') {
            encoded_uri += c;
        } else {
            encoded_uri += std::format("%{:02X}", uc);
        }
    }

    std::string result = request.substr(0, value_start) + encoded_uri;
    if (value_end < request.length()) {
        result += request.substr(value_end);
    }

    LOG_INFO("RDPSession", "replace_redirect_uri: replaced with " << actual_uri);
    return result;
}

int RDPSession::get_access_token_callback(freerdp* instance, int tokenType, char** token, size_t count, ...) {
    if (!instance || !token) return FALSE;
    
    RDPSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_session_map_mutex);
        auto it = g_session_map.find(instance);
        if (it != g_session_map.end()) {
            session = it->second;
        }
    }
    
    if (!session) {
        return FALSE;
    }

    if (!session->m_aad_callback && !session->m_token_cache_lookup_callback) {
        LOG_INFO("RDPSession", "AAD authentication required (no UI callback set)");
        LOG_INFO("RDPSession", "Token type: " << tokenType << ", count: " << count);
        return FALSE;
    }
    
    rdpClientContext* cctx = reinterpret_cast<rdpClientContext*>(instance->context);
    if (!cctx) {
        LOG_ERROR("RDPSession", "No client context available for AAD");
        return FALSE;
    }
    
    AADAuthRequest request;
    std::string scope;
    std::string req_cnf;
    
    // Extract variable arguments based on token type
    va_list ap;
    va_start(ap, count);
    
    AccessTokenType aadTokenType = static_cast<AccessTokenType>(tokenType);
    std::string cache_kind = "machine";
    std::string cache_hostname = normalize_host_key(session->m_params.hostname);

    if (aadTokenType == ACCESS_TOKEN_TYPE_AVD) {
        const std::string gateway_host = normalize_host_key(session->m_params.gateway_hostname);
        const std::string machine_host = normalize_host_key(session->m_params.hostname);
        const int request_index = session->m_avd_token_requests_seen.fetch_add(1);

        if (request_index == 0 && !gateway_host.empty()) {
            cache_kind = "gateway";
            cache_hostname = gateway_host;
        } else {
            cache_kind = "machine";
            cache_hostname = machine_host.empty() ? gateway_host : machine_host;
        }
    }

    if (session->m_token_cache_lookup_callback && !cache_hostname.empty()) {
        const auto cached_token = session->m_token_cache_lookup_callback(cache_hostname, cache_kind);
        if (cached_token.has_value() && !cached_token->empty()) {
            *token = strdup(cached_token->c_str());
            if (*token) {
                LOG_INFO("RDPSession", "Using cached AAD token for " << cache_kind
                          << " host " << cache_hostname);
                va_end(ap);
                return TRUE;
            }
        }
    }
    
    switch (aadTokenType) {
        case ACCESS_TOKEN_TYPE_AAD: {
            request.type = AADAuthRequest::Type::RDS_AAD;
            if (count >= 2) {
                const char* scope_arg = va_arg(ap, const char*);
                const char* req_cnf_arg = va_arg(ap, const char*);
                scope = scope_arg ? scope_arg : "";
                req_cnf = req_cnf_arg ? req_cnf_arg : "";
                request.scope = scope;
                request.req_cnf = req_cnf;
            }
            
            // Get the authorization URL to present to the user
            char* auth_url = freerdp_client_get_aad_url(cctx, FREERDP_CLIENT_AAD_AUTH_REQUEST, 
                                                        scope.c_str());
            if (auth_url) {
                LOG_INFO("RDPSession", "RDS_AAD auth_url: " << auth_url);
                // Pass the raw URL to the auth handler — it will rewrite the redirect_uri
                // to localhost so it can intercept the callback
                request.auth_url = auth_url;
                free(auth_url);
            }
            break;
        }
        case ACCESS_TOKEN_TYPE_AVD: {
            request.type = AADAuthRequest::Type::AVD;
            
            // Debug: Print AVD-related settings
            rdpSettings* settings = instance->context->settings;
            const char* avd_scope = freerdp_settings_get_string(settings, FreeRDP_GatewayAvdScope);
            const char* avd_client_id = freerdp_settings_get_string(settings, FreeRDP_GatewayAvdClientID);
            const char* avd_tenant = freerdp_settings_get_string(settings, FreeRDP_GatewayAvdAadtenantid);
            const char* azure_ad = freerdp_settings_get_string(settings, FreeRDP_GatewayAzureActiveDirectory);
            LOG_INFO("RDPSession", "AVD Debug Settings:");
            LOG_INFO("RDPSession", "  GatewayAvdScope: " << (avd_scope ? avd_scope : "(null)"));
            LOG_INFO("RDPSession", "  GatewayAvdClientID: " << (avd_client_id ? avd_client_id : "(null)"));
            LOG_INFO("RDPSession", "  GatewayAvdAadtenantid: " << (avd_tenant ? avd_tenant : "(null)"));
            LOG_INFO("RDPSession", "  GatewayAzureActiveDirectory: " << (azure_ad ? azure_ad : "(null)"));
            
            // Get the AVD authorization URL
            char* auth_url = freerdp_client_get_aad_url(cctx, FREERDP_CLIENT_AAD_AVD_AUTH_REQUEST);
            if (auth_url) {
                LOG_INFO("RDPSession", "  Generated auth_url: " << auth_url);
                // Pass the raw URL to the auth handler — it will rewrite the redirect_uri
                // to localhost so it can intercept the callback
                request.auth_url = auth_url;
                free(auth_url);
            } else {
                LOG_ERROR("RDPSession", "  Failed to generate auth URL - freerdp_client_get_aad_url returned NULL");
                
                // Try to manually construct the URL as a fallback
                if (avd_scope && avd_client_id) {
                    std::string fallback_url = "https://login.microsoftonline.com/";
                    fallback_url += (avd_tenant ? avd_tenant : "common");
                    fallback_url += "/oauth2/v2.0/authorize";
                    fallback_url += "?client_id=";
                    fallback_url += avd_client_id;
                    fallback_url += "&response_type=code";
                    fallback_url += "&scope=";
                    fallback_url += avd_scope;
                    // Use native client redirect URI for AVD authentication
                    fallback_url += "&redirect_uri=https%3A%2F%2Flogin.microsoftonline.com%2Fcommon%2Foauth2%2Fnativeclient";
                    request.auth_url = fallback_url;
                    LOG_INFO("RDPSession", "  Using fallback auth_url: " << fallback_url);
                }
            }
            break;
        }
        default:
            va_end(ap);
            LOG_ERROR("RDPSession", "Unknown AAD token type: " << tokenType);
            return FALSE;
    }
    va_end(ap);
    
    if (request.auth_url.empty()) {
        LOG_ERROR("RDPSession", "Failed to generate AAD auth URL");
        return FALSE;
    }

    if (!session->m_aad_callback) {
        LOG_ERROR("RDPSession", "No cached AAD token and no interactive AAD callback available");
        return FALSE;
    }
    
    LOG_INFO("RDPSession", "AAD authentication required");
    LOG_INFO("RDPSession", "  Type: " << (request.type == AADAuthRequest::Type::RDS_AAD ? "RDS_AAD" : "AVD"));
    LOG_INFO("RDPSession", "  Auth URL: " << request.auth_url);
    
    // Call the UI callback to handle the OAuth flow
    AADAuthResponse response = session->m_aad_callback(request);
    
    if (!response.success || response.redirect_url.empty()) {
        LOG_INFO("RDPSession", "AAD authentication cancelled or failed");
        return FALSE;
    }
    
    LOG_INFO("RDPSession", "AAD authentication: received redirect URL");
#ifdef VERBOSE_SECRETS
    LOG_DEBUG("RDPSession", "  Redirect URL: " << response.redirect_url);
    LOG_DEBUG("RDPSession", "  Actual redirect_uri used: " << response.actual_redirect_uri);
#endif
    
    // Extract authorization code from redirect URL
    std::string code = extract_code_from_url(response.redirect_url);
    if (code.empty()) {
        LOG_ERROR("RDPSession", "Failed to extract authorization code from redirect URL");
        return FALSE;
    }
    
    LOG_INFO("RDPSession", "AAD authentication: extracted authorization code");
    
    // Build token request URL and exchange code for token
    char* token_request_raw = nullptr;
    std::string token_request;
    switch (aadTokenType) {
        case ACCESS_TOKEN_TYPE_AAD:
            token_request_raw = freerdp_client_get_aad_url(cctx, FREERDP_CLIENT_AAD_TOKEN_REQUEST,
                                                       scope.c_str(), code.c_str(), req_cnf.c_str());
            if (token_request_raw) {
#ifdef VERBOSE_SECRETS
                LOG_DEBUG("RDPSession", "RDS_AAD Token request (original): " << token_request_raw);
#endif
                // The token request redirect_uri must match the one used in the auth request.
                // FreeRDP builds it with the original URI (ms-appx-web), but we used localhost.
                token_request = replace_redirect_uri_in_request(token_request_raw, response.actual_redirect_uri);
#ifdef VERBOSE_SECRETS
                LOG_DEBUG("RDPSession", "RDS_AAD Token request (final): " << token_request);
#endif
                free(token_request_raw);
            }
            break;
        case ACCESS_TOKEN_TYPE_AVD:
            token_request_raw = freerdp_client_get_aad_url(cctx, FREERDP_CLIENT_AAD_AVD_TOKEN_REQUEST,
                                                       code.c_str());
            if (token_request_raw) {
#ifdef VERBOSE_SECRETS
                LOG_DEBUG("RDPSession", "AVD Token request (original): " << token_request_raw);
#endif
                token_request = replace_redirect_uri_in_request(token_request_raw, response.actual_redirect_uri);
#ifdef VERBOSE_SECRETS
                LOG_DEBUG("RDPSession", "AVD Token request (final): " << token_request);
#endif
                free(token_request_raw);
            }
            break;
        default:
            return FALSE;
    }
    
    if (token_request.empty()) {
        LOG_ERROR("RDPSession", "Failed to build token request");
        return FALSE;
    }
    
    LOG_INFO("RDPSession", "AAD authentication: exchanging code for token...");
    
    // Ensure OpenSSL is initialized before making HTTPS requests
    winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);
    
    // Verify the AAD well-known endpoint is reachable before token exchange
    const char* token_ep = freerdp_utils_aad_get_wellknown_string(
        instance->context, AAD_WELLKNOWN_token_endpoint);
    if (!token_ep) {
        LOG_ERROR("RDPSession", "AAD: well-known token_endpoint is NULL — AAD module may not be initialized");
        LOG_DEBUG("RDPSession", "AAD: context=" << (void*)instance->context
                  << " rdp=" << (void*)instance->context->rdp);
        return FALSE;
    }
    LOG_DEBUG("RDPSession", "AAD: token_endpoint = " << token_ep);
    
    // Exchange code for token using FreeRDP's HTTP client
    char* token_request_cstr = strdup(token_request.c_str());
    BOOL result = client_common_get_access_token(instance, token_request_cstr, token);
    free(token_request_cstr);
    
    if (result) {
        LOG_INFO("RDPSession", "AAD authentication: successfully obtained access token");
        if (*token) {
            size_t tlen = strlen(*token);
            LOG_INFO("RDPSession", "AAD-TOKEN type=" << tokenType << " len=" << tlen);
#ifdef VERBOSE_SECRETS
            LOG_DEBUG("RDPSession", "AAD-TOKEN in full" << std::string(*token, tlen)
                      << "...");
#endif

            if (session->m_token_cache_store_callback && !cache_hostname.empty()) {
                const auto expires_at = utils::jwt_extract_expiration(*token);
                if (expires_at.has_value()) {
                    session->m_token_cache_store_callback(cache_hostname, cache_kind, *token, *expires_at);
                }
            }
        }
    } else {
        LOG_ERROR("RDPSession", "AAD authentication: failed to exchange code for token");
    }
    
    return result;
}

void RDPSession::install_callbacks(freerdp* instance) {
    if (!instance) return;
    
    // Register this session in the global map
    {
        std::lock_guard<std::mutex> lock(g_session_map_mutex);
        g_session_map[instance] = this;
    }
    
    // Install certificate verification callbacks
    instance->VerifyCertificateEx = reinterpret_cast<pVerifyCertificateEx>(verify_certificate_callback);
    instance->VerifyChangedCertificateEx = reinterpret_cast<pVerifyChangedCertificateEx>(verify_changed_certificate_callback);
    
    // Install authentication callbacks
    instance->Authenticate = reinterpret_cast<pAuthenticate>(authenticate_callback);
    instance->GatewayAuthenticate = reinterpret_cast<pAuthenticate>(gateway_authenticate_callback);
    
    // Install AAD access token callback for Azure AD authentication
    instance->GetAccessToken = reinterpret_cast<pGetAccessToken>(get_access_token_callback);
    
    LOG_INFO("RDPSession", "Installed custom callbacks for certificate, authentication, and AAD");
}

bool RDPSession::apply_settings_to_context(rdpSettings* settings) const {
    if (!settings) return false;
    
    // Server address and port
    if (!freerdp_settings_set_string(settings, FreeRDP_ServerHostname, m_params.hostname.c_str())) {
        LOG_ERROR("RDPSession", "Failed to set ServerHostname");
        return false;
    }
    freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, m_params.port);
    
    // Authentication
    if (!m_params.username.empty()) {
        freerdp_settings_set_string(settings, FreeRDP_Username, m_params.username.c_str());
    }
    if (!m_params.domain.empty()) {
        freerdp_settings_set_string(settings, FreeRDP_Domain, m_params.domain.c_str());
    }
    if (!m_params.password.empty()) {
        freerdp_settings_set_string(settings, FreeRDP_Password, m_params.password.c_str());
    }
    
    // Display settings
    freerdp_settings_set_bool(settings, FreeRDP_Fullscreen, m_params.fullscreen);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, m_params.width);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, m_params.height);
    freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, m_params.bpp);
    
    // Features
    freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard, m_params.clipboard);
    if (m_params.audio) {
        freerdp_settings_set_bool(settings, FreeRDP_AudioPlayback, true);
        freerdp_settings_set_bool(settings, FreeRDP_AudioCapture, true);
    }
    if (m_params.drive_redirection && !m_params.redirect_drive_path.empty()) {
        freerdp_settings_set_bool(settings, FreeRDP_RedirectDrives, true);
    }
    
    // Security - certificate handling
    if (m_params.ignore_certificate) {
        freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, true);
    } else {
        freerdp_settings_set_bool(settings, FreeRDP_AutoAcceptCertificate, false);
    }
    
    // Security protocol settings
    // For AAD-joined machines, we need to allow AAD auth instead of NTLM
    if (m_params.enable_rds_aad_auth || m_params.target_is_aad_joined) {
        // Enable AAD authentication
        freerdp_settings_set_bool(settings, FreeRDP_AadSecurity, true);
        LOG_INFO("RDPSession", "AAD authentication enabled");
    } else {
        // Disable Kerberos for workgroup machines - the NTLM fallback doesn't work reliably
        // when Kerberos credential acquisition fails with "Cannot find KDC" errors.
        freerdp_settings_set_string(settings, FreeRDP_AuthenticationPackageList, "!kerberos,!u2u,ntlm");
    }
    
    // ========================================================================
    // Gateway settings (including AVD/Dev Box support)
    // ========================================================================
    if (!m_params.gateway_hostname.empty()) {
        freerdp_settings_set_bool(settings, FreeRDP_GatewayEnabled, true);
        freerdp_settings_set_string(settings, FreeRDP_GatewayHostname, m_params.gateway_hostname.c_str());
        freerdp_settings_set_uint32(settings, FreeRDP_GatewayPort, m_params.gateway_port);
        
        // Gateway usage method (gatewayusagemethod)
        // 0 = Don't use gateway, 1 = Always use gateway, 2 = Use gateway if direct fails
        freerdp_settings_set_uint32(settings, FreeRDP_GatewayUsageMethod, m_params.gateway_usage_method);
        
        // For AVD/Dev Box, use HTTP transport over the gateway
        if (m_params.gateway_use_http_transport || m_params.is_avd_connection()) {
            freerdp_settings_set_bool(settings, FreeRDP_GatewayHttpTransport, true);
            freerdp_settings_set_bool(settings, FreeRDP_GatewayHttpUseWebsockets, true);
            LOG_DEBUG("RDPSession", "Gateway HTTP transport enabled for AVD connection");
        }
        
        // Gateway credentials source
        freerdp_settings_set_uint32(settings, FreeRDP_GatewayCredentialsSource, m_params.gateway_credentials_source);
        
        // Enable ARM transport for AVD
        if (m_params.is_avd_connection()) {
            freerdp_settings_set_bool(settings, FreeRDP_GatewayArmTransport, true);
            LOG_INFO("RDPSession", "Gateway ARM transport enabled for AVD");
        }
    }
    
    // ========================================================================
    // AVD/Dev Box specific settings
    // ========================================================================
    if (!m_params.load_balance_info.empty()) {
        // LoadBalanceInfo is a pointer type, use set_pointer_len
        freerdp_settings_set_pointer_len(settings, FreeRDP_LoadBalanceInfo, 
                                         m_params.load_balance_info.c_str(),
                                         m_params.load_balance_info.size());
        LOG_INFO("RDPSession", "Load balance info set: " << m_params.load_balance_info);
    }
    
    // AAD Tenant ID for Azure authentication
    if (!m_params.aad_tenant_id.empty()) {
        freerdp_settings_set_string(settings, FreeRDP_GatewayAvdAadtenantid, m_params.aad_tenant_id.c_str());
        // Tell FreeRDP to use the specific tenant ID instead of "common"
        // This affects wellknown endpoint resolution and token URLs
        if (m_params.aad_tenant_id != "common") {
            freerdp_settings_set_bool(settings, FreeRDP_GatewayAvdUseTenantid, true);
        }
        LOG_INFO("RDPSession", "AAD Tenant ID set: " << m_params.aad_tenant_id);
    }
    
    // Use nativeclient redirect URI format for AAD authentication (cross-platform support)
    // The default ms-appx-web:// scheme only works on Windows
    // nativeclient redirect URI format: https://login.microsoftonline.com/common/oauth2/nativeclient
    // Note: This setting is URL-encoded and uses %% for literal % in printf format
    // 
    // FreeRDP has two different settings depending on UseCommonStdioCallbacks:
    // - GUI mode (UseCommonStdioCallbacks=false): Uses GatewayAvdAccessTokenFormat with client_id as %s
    // - CLI mode (UseCommonStdioCallbacks=true): Uses GatewayAvdAccessAadFormat with url and tenantid as %s
    // We set both to ensure nativeclient is used regardless of mode
    freerdp_settings_set_string(settings, FreeRDP_GatewayAvdAccessTokenFormat, 
                                "https%%3A%%2F%%2Flogin.microsoftonline.com%%2Fcommon%%2Foauth2%%2Fnativeclient");
    // For CLI mode: the format string expects (url, tenantid) but we want a fixed URI
    // So we use a format that ignores the arguments
    freerdp_settings_set_string(settings, FreeRDP_GatewayAvdAccessAadFormat,
                                "https%%3A%%2F%%2Flogin.microsoftonline.com%%2Fcommon%%2Foauth2%%2Fnativeclient");
    LOG_INFO("RDPSession", "Using nativeclient redirect URI for AAD authentication");
    
    // AVD-specific settings
    if (!m_params.arm_path.empty()) {
        freerdp_settings_set_string(settings, FreeRDP_GatewayAvdArmpath, m_params.arm_path.c_str());
    }
    if (!m_params.wvd_endpoint_pool.empty()) {
        freerdp_settings_set_string(settings, FreeRDP_GatewayAvdWvdEndpointPool, m_params.wvd_endpoint_pool.c_str());
    }
    if (!m_params.workspace_id.empty()) {
        // workspace_id is an AVD-specific field not directly consumed by FreeRDP core.
        // It's used for hub discovery and diagnostics, not for connection setup.
        LOG_INFO("RDPSession", "Workspace ID: " << m_params.workspace_id);
    }
    
    // Remote application program (for RemoteApp connections or AVD ARM transport)
    // AVD/DevBox RDP files have remoteapplicationprogram:s:||<GUID> even for desktop sessions
    // RemoteApplicationMode should NOT be set for desktop sessions (remoteapplicationmode:i:0)
    if (!m_params.remote_application_program.empty()) {
        freerdp_settings_set_string(settings, FreeRDP_RemoteApplicationProgram, m_params.remote_application_program.c_str());
        LOG_INFO("RDPSession", "RemoteApplicationProgram set: " << m_params.remote_application_program);
    }
    
    // ========================================================================
    // Performance options
    // ========================================================================
    freerdp_settings_set_bool(settings, FreeRDP_DisableWallpaper, m_params.disable_wallpaper);
    freerdp_settings_set_bool(settings, FreeRDP_DisableThemes, m_params.disable_themes);
    freerdp_settings_set_bool(settings, FreeRDP_AllowFontSmoothing, !m_params.disable_font_smoothing);
    
    // Dynamic resolution support (use param if set, otherwise default to true)
    if (m_params.dynamic_resolution) {
        freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate, true);
        freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, true);
    }
    
    // Window title - use remote desktop name if available
    std::string window_title = m_params.remote_desktop_name.empty() 
                               ? m_params.hostname 
                               : m_params.remote_desktop_name;
    freerdp_settings_set_string(settings, FreeRDP_WindowTitle, window_title.c_str());
    
    // ========================================================================
    // Additional redirection settings from RDP file
    // ========================================================================
    if (m_params.redirect_printers) {
        freerdp_settings_set_bool(settings, FreeRDP_RedirectPrinters, true);
    }
    if (m_params.redirect_smart_cards) {
        freerdp_settings_set_bool(settings, FreeRDP_RedirectSmartCards, true);
    }
    if (m_params.redirect_com_ports) {
        freerdp_settings_set_bool(settings, FreeRDP_RedirectSerialPorts, true);
    }
    
    // Audio mode from RDP file
    // audiomode: 0=bring to local, 1=leave at remote, 2=do not play
    if (m_params.audio_mode == 0) {
        freerdp_settings_set_bool(settings, FreeRDP_AudioPlayback, true);
        freerdp_settings_set_bool(settings, FreeRDP_RemoteConsoleAudio, false);
    } else if (m_params.audio_mode == 1) {
        freerdp_settings_set_bool(settings, FreeRDP_AudioPlayback, false);
        freerdp_settings_set_bool(settings, FreeRDP_RemoteConsoleAudio, true);
    } else {
        freerdp_settings_set_bool(settings, FreeRDP_AudioPlayback, false);
        freerdp_settings_set_bool(settings, FreeRDP_RemoteConsoleAudio, false);
    }
    
    // Audio capture
    if (m_params.audio_capture_mode == 1) {
        freerdp_settings_set_bool(settings, FreeRDP_AudioCapture, true);
    }
    
    // ========================================================================
    // Advanced RDP Features (GUI Options)
    // ========================================================================
    
    // +home-drive: Map home directory as drive
    if (m_params.home_drive) {
        freerdp_settings_set_bool(settings, FreeRDP_RedirectHomeDrive, true);
    }
    
    // /cert:tofu - Trust On First Use for certificates
    if (m_params.cert_tofu) {
        freerdp_settings_set_bool(settings, FreeRDP_AutoAcceptCertificate, true);
    }
    
    // /usb:auto - Automatic USB redirection
    // Note: USB redirection requires additional setup; this enables drive redirection as fallback
    if (m_params.usb_auto) {
        freerdp_settings_set_bool(settings, FreeRDP_RedirectDrives, true);
    }
    
    // /floatbar - Floating toolbar in fullscreen (UINT32: 0=disabled, non-zero=enabled)
    if (m_params.floatbar) {
        freerdp_settings_set_uint32(settings, FreeRDP_Floatbar, 1);
    }
    
    // /network:auto - Automatic network detection
    if (m_params.network_auto) {
        freerdp_settings_set_uint32(settings, FreeRDP_ConnectionType, CONNECTION_TYPE_AUTODETECT);
        freerdp_settings_set_bool(settings, FreeRDP_NetworkAutoDetect, true);
    }
    
    // /gfx:AVC420 - AVC420 graphics mode (H.264)
    if (m_params.gfx_avc420) {
        freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, true);
        freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444v2, false);
        freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444, false);
        freerdp_settings_set_bool(settings, FreeRDP_GfxH264, true);
    }
    
    // /compression - Enable compression
    if (m_params.compression) {
        freerdp_settings_set_bool(settings, FreeRDP_CompressionEnabled, true);
        freerdp_settings_set_uint32(settings, FreeRDP_CompressionLevel, 2);
    }
    
    // /audio:sys:pulse - PulseAudio sound output
    // Note: Audio device selection is handled by channel plugins, we just enable audio here
    if (m_params.audio_pulse) {
        freerdp_settings_set_bool(settings, FreeRDP_AudioPlayback, true);
        freerdp_settings_set_bool(settings, FreeRDP_AudioCapture, true);
    }
    
    // /prevent-session-lock - Prevent remote session from locking
    // Use FakeMouseMotionInterval to send fake mouse movements (interval in ms, 0 = disabled)
    if (m_params.prevent_session_lock) {
        freerdp_settings_set_uint32(settings, FreeRDP_FakeMouseMotionInterval, 60000); // Every 60 seconds
    }
    
    // /auto-reconnect - Enable automatic reconnection
    // For AVD connections, always enable auto-reconnect since the VM may need
    // to boot from a deallocated state (ARM transport returns HTTP 400 with a
    // "retry after 5 minutes" message). Without this, FreeRDP will abort
    // immediately on ERRCONNECT_TARGET_BOOTING.
    if (m_params.auto_reconnect || m_params.is_avd_connection()) {
        freerdp_settings_set_bool(settings, FreeRDP_AutoReconnectionEnabled, true);
        int max_retries = m_params.auto_reconnect_max_retries;
        // AVD VM boot can take up to 5 minutes; use at least 20 retries
        // (each retry delays by TcpConnectTimeout, default ~15s)
        if (m_params.is_avd_connection() && max_retries < 20) {
            max_retries = 20;
        }
        freerdp_settings_set_uint32(settings, FreeRDP_AutoReconnectMaxRetries, 
                                    static_cast<uint32_t>(max_retries));
        LOG_INFO("RDPSession", "Auto-reconnect enabled (max retries: " << max_retries << ")");
    }
    
    return true;
}

bool RDPSession::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_running) {
        return false;  // Already running
    }
    
    m_running = true;
    m_state = RDPConnectionState::Connecting;
    
    // Start the session in a separate thread
    m_session_thread = std::thread(&RDPSession::session_thread_func, this);
    
    return true;
}

void RDPSession::session_thread_func() {
    rdpContext* context = nullptr;
    
    // Initialize FreeRDP X11 client entry points
    RDP_CLIENT_ENTRY_POINTS clientEntryPoints = { 0 };
    clientEntryPoints.Size = sizeof(RDP_CLIENT_ENTRY_POINTS);
    clientEntryPoints.Version = RDP_CLIENT_INTERFACE_VERSION;
    
    if (RdpClientEntry(&clientEntryPoints) != 0) {
        LOG_ERROR("RDPSession", "Failed to initialize FreeRDP client entry points");
        m_state = RDPConnectionState::Error;
        m_running = false;
        return;
    }
    
    // Create client context
    context = freerdp_client_context_new(&clientEntryPoints);
    if (!context) {
        LOG_ERROR("RDPSession", "Failed to create FreeRDP client context");
        m_state = RDPConnectionState::Error;
        m_running = false;
        return;
    }
    
    // Store context for later cleanup
    m_context = context;
    
    // Apply connection parameters to settings
    rdpSettings* settings = context->settings;
    if (!apply_settings_to_context(settings)) {
        LOG_ERROR("RDPSession", "Failed to apply RDP settings");
        freerdp_client_context_free(context);
        m_context = nullptr;
        m_state = RDPConnectionState::Error;
        m_running = false;
        return;
    }
    
    // Install our custom callbacks for certificate verification and authentication
    install_callbacks(context->instance);
    
    LOG_INFO("RDPSession", "Starting FreeRDP X11 client for " << m_params.hostname);
    
    // Start the client (this spawns the X11 window)
    if (freerdp_client_start(context) != 0) {
        LOG_ERROR("RDPSession", "Failed to start FreeRDP client");
        // Remove from global session map before cleanup
        if (context->instance) {
            std::lock_guard<std::mutex> lock(g_session_map_mutex);
            g_session_map.erase(context->instance);
        }
        freerdp_client_context_free(context);
        m_context = nullptr;
        m_state = RDPConnectionState::Error;
        m_running = false;
        return;
    }
    
    m_state = RDPConnectionState::Connected;
    
    // Get the client thread and wait for it to finish
    HANDLE thread = freerdp_client_get_thread(context);
    if (thread) {
        DWORD exitCode = 0;
        WaitForSingleObject(thread, INFINITE);
        GetExitCodeThread(thread, &exitCode);
        
        RDPConnectionState newState = (exitCode == 0)
            ? RDPConnectionState::Disconnected
            : RDPConnectionState::Error;
        m_state = newState;
    } else {
        m_state = RDPConnectionState::Error;
    }
    
    // Stop and cleanup
    freerdp_client_stop(context);
    
    // Remove from global session map
    if (context->instance) {
        std::lock_guard<std::mutex> lock(g_session_map_mutex);
        g_session_map.erase(context->instance);
    }
    
    freerdp_client_context_free(context);
    m_context = nullptr;
    
    m_running = false;
}

void RDPSession::stop() {
    // Stop the FreeRDP client if context is valid and still running
    if (m_running && m_context) {
        LOG_INFO("RDPSession", "Stopping FreeRDP client session");
        freerdp_client_stop(static_cast<rdpContext*>(m_context));
    }
    
    // Always join the thread if it's joinable (even if m_running is false)
    if (m_session_thread.joinable()) {
        m_session_thread.join();
    }
}

bool RDPSession::is_active() const {
    return m_running;
}

RDPConnectionState RDPSession::get_state() const {
    return m_state;
}

// ============================================================================
// RDPLauncher Implementation
// ============================================================================

RDPLauncher::RDPLauncher() {
    LOG_INFO("RDPLauncher", "Initialized (using FreeRDP library API). Version: " 
              << get_freerdp_version());
}

RDPLauncher::~RDPLauncher() {
    terminate_all();
}

bool RDPLauncher::launch(const RDPConnectionParams& params) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    LOG_INFO("RDPLauncher", "Launching RDP session to: " << params.hostname 
              << ":" << params.port);
    
    // Clean up finished sessions
    cleanup_sessions();
    
    // Create new session using library API
    auto session = std::make_shared<RDPSession>(params);
    
    // Propagate callbacks to the session
    if (m_cert_callback) {
        session->set_certificate_callback(m_cert_callback);
    }
    if (m_auth_callback) {
        session->set_authenticate_callback(m_auth_callback);
    }
    if (m_aad_callback) {
        session->set_aad_auth_callback(m_aad_callback);
    }
    if (m_token_cache_lookup_callback) {
        session->set_token_cache_lookup_callback(m_token_cache_lookup_callback);
    }
    if (m_token_cache_store_callback) {
        session->set_token_cache_store_callback(m_token_cache_store_callback);
    }
    
    auto sessionStartResult = session->start();

    if (!sessionStartResult) {
        m_last_error = "Failed to start RDP session";
        return false;
    }
    
    m_sessions.push_back(session);
    
    // Notify callback if set
    if (m_state_callback) {
        m_state_callback(RDPConnectionState::Connecting, 
                        "Connecting to " + params.hostname);
    }
    
    return true;
}

bool RDPLauncher::launch_embedded(const RDPConnectionParams& params) {
    // With library integration, launch() now uses the library API directly
    // This method can be used for future window embedding if needed
    return launch(params);
}

std::string RDPLauncher::get_last_error() const {
    return m_last_error;
}

std::string RDPLauncher::get_freerdp_version() const {
    return FREERDP_VERSION_FULL;
}

std::vector<std::shared_ptr<RDPSession>> RDPLauncher::get_active_sessions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::shared_ptr<RDPSession>> active;
    for (const auto& session : m_sessions) {
        if (session->is_active()) {
            active.push_back(session);
        }
    }
    return active;
}

void RDPLauncher::set_state_callback(ConnectionStateCallback callback) {
    m_state_callback = std::move(callback);
}

void RDPLauncher::set_certificate_callback(CertificateVerifyCallback callback) {
    m_cert_callback = std::move(callback);
}

void RDPLauncher::set_authenticate_callback(AuthenticateCallback callback) {
    m_auth_callback = std::move(callback);
}

void RDPLauncher::set_aad_auth_callback(AADAuthCallback callback) {
    m_aad_callback = std::move(callback);
}

void RDPLauncher::set_token_cache_lookup_callback(TokenCacheLookupCallback callback) {
    m_token_cache_lookup_callback = std::move(callback);
}

void RDPLauncher::set_token_cache_store_callback(TokenCacheStoreCallback callback) {
    m_token_cache_store_callback = std::move(callback);
}

void RDPLauncher::terminate_all() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (auto& session : m_sessions) {
        session->stop();
    }
    m_sessions.clear();
}

void RDPLauncher::cleanup_sessions() {
    m_sessions.erase(
        std::remove_if(m_sessions.begin(), m_sessions.end(),
            [](const std::shared_ptr<RDPSession>& s) {
                return !s->is_active();
            }),
        m_sessions.end()
    );
}
