#pragma once

/**
 * RDP Launcher - FreeRDP Integration Module
 * 
 * Uses FreeRDP library API to create and manage RDP client connections.
 * Directly integrates with the X11 FreeRDP client for popup windows.
 */

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <cstdint>

// Forward declarations for FreeRDP types
struct rdp_settings;
typedef struct rdp_settings rdpSettings;
struct rdp_freerdp;
typedef struct rdp_freerdp freerdp;

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

/**
 * AAD Authentication request - for OAuth2 code flow
 */
struct AADAuthRequest {
    enum Type {
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
};

/**
 * Callback types for interactive dialogs
 * These allow the UI to handle certificate verification and credential prompts
 */
using CertificateVerifyCallback = std::function<CertificateAcceptance(const CertificateInfo& info)>;
using AuthenticateCallback = std::function<AuthResponse(const AuthRequest& request)>;

/**
 * Callback for AAD authentication - allows UI to handle OAuth flow
 * Return AADAuthResponse with success=true and the redirect_url containing the authorization code
 */
using AADAuthCallback = std::function<AADAuthResponse(const AADAuthRequest& request)>;

/**
 * RDP Connection Parameters
 */
struct RDPConnectionParams {
    std::string hostname;
    int port = 3389;
    std::string username;
    std::string password;  // Optional - will prompt if empty
    std::string domain;
    
    // Display settings
    int width = 1920;
    int height = 1080;
    int bpp = 32;
    bool fullscreen = false;
    
    // Features
    bool clipboard = true;
    bool audio = true;
    bool drive_redirection = false;
    std::string redirect_drive_path;
    
    // Advanced Features (RDP Flags)
    bool home_drive = false;           // +home-drive: Map home directory
    bool cert_tofu = false;            // /cert:tofu: Trust on first use
    bool usb_auto = false;             // /usb:auto: Auto USB redirection
    bool floatbar = false;             // /floatbar: Floating toolbar
    bool dynamic_resolution = false;   // /dynamic-resolution
    bool network_auto = false;         // /network:auto
    bool gfx_avc420 = false;           // /gfx:AVC420
    bool compression = false;          // /compression
    bool audio_pulse = false;          // /audio:sys:pulse
    bool prevent_session_lock = false; // /prevent-session-lock
    bool auto_reconnect = false;       // /auto-reconnect
    int auto_reconnect_max_retries = 3; // /auto-reconnect-max-retries
    
    // Security
    bool ignore_certificate = false;
    std::string gateway_hostname;
    int gateway_port = 443;
    
    // Performance
    bool disable_wallpaper = false;
    bool disable_themes = false;
    bool disable_font_smoothing = false;
    
    // ========================================================================
    // AVD / Dev Box / Azure AD specific settings
    // ========================================================================
    
    // Azure AD Authentication
    bool target_is_aad_joined = false;     // Target is Azure AD joined
    bool enable_rds_aad_auth = false;      // Enable RDS AAD authentication
    std::string aad_tenant_id;             // Azure AD tenant ID
    
    // Gateway settings (for AVD/WVD)
    int gateway_usage_method = 0;          // 0=none, 1=always, 2=detect
    int gateway_credentials_source = 0;    // 0=any, 1=smartcard, 4=ask
    int gateway_brokering_type = 0;        // Gateway brokering type
    bool gateway_use_http_transport = true; // Use HTTP transport for gateway
    
    // AVD/WVD specific
    std::string load_balance_info;         // Load balance info string
    std::string wvd_endpoint_pool;         // WVD endpoint pool ID
    std::string arm_path;                  // Azure Resource Manager path
    std::string workspace_id;              // Workspace ID
    std::string remote_application_program; // Remote app program
    std::string remote_desktop_name;       // Remote desktop display name
    
    // Redirection settings from RDP file
    bool redirect_printers = false;
    bool redirect_smart_cards = false;
    bool redirect_com_ports = false;
    bool redirect_location = false;
    std::string drives_to_redirect;
    std::string cameras_to_redirect;
    std::string usb_devices_to_redirect;
    
    // Audio from RDP file
    int audio_mode = 0;                    // 0=local, 1=remote, 2=none
    int audio_capture_mode = 0;            // Audio capture
    
    // Helper to check if this is an AVD/Dev Box connection
    bool is_avd_connection() const {
        return !wvd_endpoint_pool.empty() || !arm_path.empty() || 
               !load_balance_info.empty() || enable_rds_aad_auth;
    }
    
    // Helper to check if gateway is required
    bool uses_gateway() const {
        return !gateway_hostname.empty() && gateway_usage_method > 0;
    }
};

/**
 * Connection state enumeration
 */
enum class RDPConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Error
};

/**
 * Callback types for connection events
 */
using ConnectionStateCallback = std::function<void(RDPConnectionState state, const std::string& message)>;

/**
 * RDP Session - represents an active RDP connection
 */
class RDPSession {
public:
    RDPSession(const RDPConnectionParams& params);
    ~RDPSession();
    
    // Non-copyable
    RDPSession(const RDPSession&) = delete;
    RDPSession& operator=(const RDPSession&) = delete;
    
    // Start the RDP session
    bool start();
    
    // Stop the session
    void stop();
    
    // Check if session is active
    bool is_active() const;
    
    // Get session state
    RDPConnectionState get_state() const;
    
    // Get the process ID (for subprocess approach)
    int get_pid() const { return m_pid; }
    
    // Set callbacks for interactive dialogs (must be set before start())
    void set_certificate_callback(CertificateVerifyCallback callback);
    void set_authenticate_callback(AuthenticateCallback callback);
    void set_aad_auth_callback(AADAuthCallback callback);

private:
    RDPConnectionParams m_params;
    std::atomic<RDPConnectionState> m_state;
    std::atomic<bool> m_running;
    std::thread m_session_thread;
    int m_pid = -1;
    mutable std::mutex m_mutex;
    void* m_context = nullptr;  // FreeRDP rdpContext pointer
    
    // Callbacks for interactive dialogs
    CertificateVerifyCallback m_cert_callback;
    AuthenticateCallback m_auth_callback;
    AADAuthCallback m_aad_callback;
    
    // Internal methods
    void session_thread_func();
    bool apply_settings_to_context(rdpSettings* settings) const;
    void install_callbacks(freerdp* instance);
    
    // Static callback trampolines (called by FreeRDP)
    // Using uint32_t/int for portability since DWORD/BOOL are Windows types
    static uint32_t verify_certificate_callback(freerdp* instance, const char* host, uint16_t port,
                                             const char* common_name, const char* subject,
                                             const char* issuer, const char* fingerprint, uint32_t flags);
    static uint32_t verify_changed_certificate_callback(freerdp* instance, const char* host, uint16_t port,
                                                     const char* common_name, const char* subject,
                                                     const char* issuer, const char* new_fingerprint,
                                                     const char* old_subject, const char* old_issuer,
                                                     const char* old_fingerprint, uint32_t flags);
    static int authenticate_callback(freerdp* instance, char** username, char** password, char** domain);
    static int gateway_authenticate_callback(freerdp* instance, char** username, char** password, char** domain);
    static int get_access_token_callback(freerdp* instance, int tokenType, char** token, size_t count, ...);
    
    // Helper for AAD code extraction
    static std::string extract_code_from_url(const std::string& url);
};

/**
 * RDP Launcher - manages RDP connections
 */
class RDPLauncher {
public:
    RDPLauncher();
    ~RDPLauncher();
    
    /**
     * Launch a new RDP connection
     * 
     * @param params Connection parameters
     * @return true if launch was successful
     */
    bool launch(const RDPConnectionParams& params);
    
    /**
     * Launch using the FreeRDP library API (embedded window)
     * More complex but allows tighter integration
     */
    bool launch_embedded(const RDPConnectionParams& params);
    
    /**
     * Get the last error message
     */
    std::string get_last_error() const;
    
    /**
     * Get FreeRDP version string
     */
    std::string get_freerdp_version() const;
    
    /**
     * Get list of active sessions
     */
    std::vector<std::shared_ptr<RDPSession>> get_active_sessions() const;
    
    /**
     * Set callback for connection state changes
     */
    void set_state_callback(ConnectionStateCallback callback);
    
    /**
     * Set callback for certificate verification dialogs
     * This will be propagated to new sessions
     */
    void set_certificate_callback(CertificateVerifyCallback callback);
    
    /**
     * Set callback for authentication dialogs
     * This will be propagated to new sessions
     */
    void set_authenticate_callback(AuthenticateCallback callback);

    /**
     * Set callback for AAD (Azure AD) authentication
     * This will be propagated to new sessions
     */
    void set_aad_auth_callback(AADAuthCallback callback);

    /**
     * Terminate all active sessions
     */
    void terminate_all();

private:
    std::string m_last_error;
    std::vector<std::shared_ptr<RDPSession>> m_sessions;
    ConnectionStateCallback m_state_callback;
    CertificateVerifyCallback m_cert_callback;
    AuthenticateCallback m_auth_callback;
    AADAuthCallback m_aad_callback;
    mutable std::mutex m_mutex;
    
    // Clean up finished sessions
    void cleanup_sessions();
};