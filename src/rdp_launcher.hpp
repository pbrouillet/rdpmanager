#pragma once

/**
 * RDP Launcher - FreeRDP Integration Module
 * 
 * Handles spawning and managing FreeRDP client connections.
 */

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

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
    
private:
    RDPConnectionParams m_params;
    std::atomic<RDPConnectionState> m_state;
    std::atomic<bool> m_running;
    std::thread m_session_thread;
    int m_pid = -1;
    mutable std::mutex m_mutex;
    
    // Internal methods
    void session_thread_func();
    std::vector<std::string> build_command_args() const;
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
     * Terminate all active sessions
     */
    void terminate_all();
    
private:
    std::string m_last_error;
    std::vector<std::shared_ptr<RDPSession>> m_sessions;
    ConnectionStateCallback m_state_callback;
    mutable std::mutex m_mutex;
    
    // Locate the xfreerdp/wfreerdp executable
    std::string find_freerdp_executable() const;
    
    // Clean up finished sessions
    void cleanup_sessions();
};
