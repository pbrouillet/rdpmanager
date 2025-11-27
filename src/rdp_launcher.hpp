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
 * Callback types for interactive dialogs
 * These allow the UI to handle certificate verification and credential prompts
 */
using CertificateVerifyCallback = std::function<CertificateAcceptance(const CertificateInfo& info)>;
using AuthenticateCallback = std::function<AuthResponse(const AuthRequest& request)>;

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
     * Terminate all active sessions
     */
    void terminate_all();

private:
    std::string m_last_error;
    std::vector<std::shared_ptr<RDPSession>> m_sessions;
    ConnectionStateCallback m_state_callback;
    CertificateVerifyCallback m_cert_callback;
    AuthenticateCallback m_auth_callback;
    mutable std::mutex m_mutex;
    
    // Clean up finished sessions
    void cleanup_sessions();
};