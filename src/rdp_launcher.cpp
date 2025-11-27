/**
 * RDP Launcher Implementation
 * 
 * Uses FreeRDP library API to create RDP client connections.
 * Directly links to the X11 FreeRDP client for popup windows.
 */

#include "rdp_launcher.hpp"

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cstring>

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

// For UINT16, DWORD, BOOL types
#include <winpr/wtypes.h>

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
        std::cout << "[RDPSession] Certificate verification required (no UI callback set)" << std::endl;
        std::cout << "  Host: " << (host ? host : "") << ":" << port << std::endl;
        std::cout << "  CN: " << (common_name ? common_name : "") << std::endl;
        std::cout << "  Fingerprint: " << (fingerprint ? fingerprint : "") << std::endl;
        // Return 0 to reject, as we can't show a dialog
        return 0;
    }
    
    CertificateInfo info;
    info.host = host ? host : "";
    info.port = port;
    info.common_name = common_name ? common_name : "";
    info.subject = subject ? subject : "";
    info.issuer = issuer ? issuer : "";
    info.fingerprint = fingerprint ? fingerprint : "";
    info.is_changed = false;
    
    CertificateAcceptance result = session->m_cert_callback(info);
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
        std::cout << "[RDPSession] Changed certificate verification required (no UI callback set)" << std::endl;
        return 0;
    }
    
    CertificateInfo info;
    info.host = host ? host : "";
    info.port = port;
    info.common_name = common_name ? common_name : "";
    info.subject = subject ? subject : "";
    info.issuer = issuer ? issuer : "";
    info.fingerprint = new_fingerprint ? new_fingerprint : "";
    info.is_changed = true;
    info.old_fingerprint = old_fingerprint ? old_fingerprint : "";
    
    CertificateAcceptance result = session->m_cert_callback(info);
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
        std::cout << "[RDPSession] Authentication required (no UI callback set)" << std::endl;
        return 0;
    }
    
    AuthRequest request;
    if (instance->context && instance->context->settings) {
        request.hostname = freerdp_settings_get_string(instance->context->settings, FreeRDP_ServerHostname);
    }
    request.is_gateway = false;
    request.current_username = (username && *username) ? *username : "";
    request.current_domain = (domain && *domain) ? *domain : "";
    
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
        std::cout << "[RDPSession] Gateway authentication required (no UI callback set)" << std::endl;
        return 0;
    }
    
    AuthRequest request;
    if (instance->context && instance->context->settings) {
        request.hostname = freerdp_settings_get_string(instance->context->settings, FreeRDP_GatewayHostname);
    }
    request.is_gateway = true;
    request.current_username = (username && *username) ? *username : "";
    request.current_domain = (domain && *domain) ? *domain : "";
    
    AuthResponse response = session->m_auth_callback(request);
    
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
    
    std::cout << "[RDPSession] Installed custom callbacks for certificate and authentication" << std::endl;
}

bool RDPSession::apply_settings_to_context(rdpSettings* settings) const {
    if (!settings) return false;
    
    // Server address and port
    if (!freerdp_settings_set_string(settings, FreeRDP_ServerHostname, m_params.hostname.c_str())) {
        std::cerr << "[ERROR] Failed to set ServerHostname" << std::endl;
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
    // Disable Kerberos for workgroup machines - the NTLM fallback doesn't work reliably
    // when Kerberos credential acquisition fails with "Cannot find KDC" errors.
    // For domain-joined machines, you may want to remove or make this configurable.
    freerdp_settings_set_string(settings, FreeRDP_AuthenticationPackageList, "!kerberos,!u2u,ntlm");
    
    // Gateway settings
    if (!m_params.gateway_hostname.empty()) {
        freerdp_settings_set_bool(settings, FreeRDP_GatewayEnabled, true);
        freerdp_settings_set_string(settings, FreeRDP_GatewayHostname, m_params.gateway_hostname.c_str());
        freerdp_settings_set_uint32(settings, FreeRDP_GatewayPort, m_params.gateway_port);
    }
    
    // Performance options
    freerdp_settings_set_bool(settings, FreeRDP_DisableWallpaper, m_params.disable_wallpaper);
    freerdp_settings_set_bool(settings, FreeRDP_DisableThemes, m_params.disable_themes);
    freerdp_settings_set_bool(settings, FreeRDP_AllowFontSmoothing, !m_params.disable_font_smoothing);
    
    // Dynamic resolution support
    freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate, true);
    freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, true);
    
    // Window title
    freerdp_settings_set_string(settings, FreeRDP_WindowTitle, m_params.hostname.c_str());
    
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
        std::cerr << "[ERROR] Failed to initialize FreeRDP client entry points" << std::endl;
        m_state = RDPConnectionState::Error;
        m_running = false;
        return;
    }
    
    // Create client context
    context = freerdp_client_context_new(&clientEntryPoints);
    if (!context) {
        std::cerr << "[ERROR] Failed to create FreeRDP client context" << std::endl;
        m_state = RDPConnectionState::Error;
        m_running = false;
        return;
    }
    
    // Store context for later cleanup
    m_context = context;
    
    // Apply connection parameters to settings
    rdpSettings* settings = context->settings;
    if (!apply_settings_to_context(settings)) {
        std::cerr << "[ERROR] Failed to apply RDP settings" << std::endl;
        freerdp_client_context_free(context);
        m_context = nullptr;
        m_state = RDPConnectionState::Error;
        m_running = false;
        return;
    }
    
    // Install our custom callbacks for certificate verification and authentication
    install_callbacks(context->instance);
    
    std::cout << "[RDPSession] Starting FreeRDP X11 client for " << m_params.hostname << std::endl;
    
    // Start the client (this spawns the X11 window)
    if (freerdp_client_start(context) != 0) {
        std::cerr << "[ERROR] Failed to start FreeRDP client" << std::endl;
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
        
        m_state = (exitCode == 0)
            ? RDPConnectionState::Disconnected
            : RDPConnectionState::Error;
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
    if (!m_running) {
        return;
    }
    
    // Stop the FreeRDP client if context is valid
    if (m_context) {
        std::cout << "[RDPSession] Stopping FreeRDP client session" << std::endl;
        freerdp_client_stop(static_cast<rdpContext*>(m_context));
    }
    
    // Wait for thread to finish
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
    std::cout << "[RDPLauncher] Initialized (using FreeRDP library API). Version: " 
              << get_freerdp_version() << std::endl;
}

RDPLauncher::~RDPLauncher() {
    terminate_all();
}

bool RDPLauncher::launch(const RDPConnectionParams& params) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::cout << "[RDPLauncher] Launching RDP session to: " << params.hostname 
              << ":" << params.port << std::endl;
    
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
