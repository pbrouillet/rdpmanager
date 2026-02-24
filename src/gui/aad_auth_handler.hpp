/**
 * AAD Auth Handler - Handles Azure AD OAuth authentication flows
 * 
 * This module manages the AAD authentication window and OAuth redirect
 * interception for Azure AD, AVD, and RDS AAD authentication.
 */

#ifndef AAD_AUTH_HANDLER_HPP
#define AAD_AUTH_HANDLER_HPP

#include <webui.hpp>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <string>
#include <chrono>

#include "../rdp_launcher.hpp"  // For AADAuthRequest, AADAuthResponse, AADAuthCallback

/**
 * AADAuthHandler manages the OAuth authentication flow for Azure AD.
 * 
 * It opens a native WebView window for the OAuth login, intercepts the
 * redirect URL containing the authorization code, and returns the result.
 */
class AADAuthHandler {
public:
    AADAuthHandler();
    ~AADAuthHandler();

    // Non-copyable, non-movable
    AADAuthHandler(const AADAuthHandler&) = delete;
    AADAuthHandler& operator=(const AADAuthHandler&) = delete;
    AADAuthHandler(AADAuthHandler&&) = delete;
    AADAuthHandler& operator=(AADAuthHandler&&) = delete;

    /**
     * Enable manual code flow mode.
     * In this mode, the OAuth URL is printed to the console and the user
     * must manually copy/paste it into their browser, then paste the
     * redirect URL back into the console.
     */
    static void enable_manual_code_flow();

    /**
     * Check if manual code flow mode is enabled
     */
    static bool is_manual_code_flow_enabled();

    /**
     * Enable verbose AAD debug logging.
     * Logs every navigation URL with parsed query parameters, and
     * captures HTTP request/response details on the callback server.
     */
    static void enable_debug();

    /**
     * Check if AAD debug logging is enabled
     */
    static bool is_debug_enabled();

    /**
     * Set the main window for displaying toast notifications
     */
    void set_main_window(webui::window* window);

    /**
     * Get the callback function to register with RDPLauncher
     */
    AADAuthCallback get_callback();

    /**
     * Handle AAD auth response from JavaScript (if using embedded flow)
     * @param success Whether OAuth flow completed successfully
     * @param redirect_url The redirect URL containing the authorization code
     */
    void on_response(bool success, const std::string& redirect_url);

    /**
     * Check if an AAD authentication is currently in progress
     */
    bool is_pending() const {
        bool pending = m_pending.load();
        return pending;
    }

private:
    /**
     * Main authentication handler - called from FreeRDP thread
     * Opens a native WebView window for OAuth login
     */
    AADAuthResponse handle_authenticate(const AADAuthRequest& request);

    /**
     * Handle manual code flow - prints URL to console and waits for user input
     * Called when manual code flow mode is enabled
     */
    AADAuthResponse handle_manual_code_flow(const AADAuthRequest& request,
                                           std::unique_lock<std::mutex>& lock);

    /**
     * Static event handler for WebUI callbacks
     */
    static void s_handle_window_events(webui::window::event* e);
    
    /**
     * Static handler for OAuth callback from JavaScript
     */
    static void s_handle_oauth_callback(webui::window::event* e);
    
    /**
     * Static handler for console.log forwarding from browser
     */
    static void s_handle_log_to_backend(webui::window::event* e);

    /**
     * Process navigation events to intercept OAuth redirects
     */
    void process_navigation(const std::string& url);

    /**
     * Handle window disconnection (user closed the window)
     */
    void handle_disconnected();

    // ========================================================================
    // State and synchronization
    // ========================================================================
    
    webui::window* m_main_window = nullptr;
    
    std::mutex m_mutex;
    std::condition_variable m_cv;

    // Authentication state
    std::atomic<bool> m_pending{false};
    std::atomic<bool> m_navigating_to_oauth{false};  // True when we've redirected to OAuth URL
    std::atomic<bool> m_callback_complete{false};     // True when WebUI callback has fully returned
    AADAuthRequest m_request;
    AADAuthResponse m_result;
    
    // Original redirect URI for URL reconstruction (ms-appx-web:// or nativeclient)
    std::string m_original_redirect_uri;
    
    // The actual localhost redirect URI used in the auth request
    std::string m_actual_redirect_uri;

    // OAuth window
    std::unique_ptr<webui::window> m_window;
    std::mutex m_window_mutex;

    // Timeout for OAuth flow (5 minutes)
    static constexpr auto AUTH_TIMEOUT = std::chrono::seconds(300);

    // Static instance pointer for WebUI callbacks
    static AADAuthHandler* s_instance;

    // Static flag for manual code flow mode
    static bool s_manual_code_flow;

    // Static flag for AAD debug logging
    static bool s_aad_debug;

    // Helper: parse and log URL query parameters
    static void log_url_details(const std::string& url, const std::string& context);
};

#endif // AAD_AUTH_HANDLER_HPP
