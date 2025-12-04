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
    bool is_pending() const { return m_pending.load(); }

private:
    /**
     * Main authentication handler - called from FreeRDP thread
     * Opens a native WebView window for OAuth login
     */
    AADAuthResponse handle_authenticate(const AADAuthRequest& request);

    /**
     * Static event handler for WebUI callbacks
     */
    static void s_handle_window_events(webui::window::event* e);

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
    AADAuthRequest m_request;
    AADAuthResponse m_result;

    // OAuth window
    std::unique_ptr<webui::window> m_window;
    std::mutex m_window_mutex;

    // Timeout for OAuth flow (5 minutes)
    static constexpr auto AUTH_TIMEOUT = std::chrono::seconds(300);

    // Static instance pointer for WebUI callbacks
    static AADAuthHandler* s_instance;
};

#endif // AAD_AUTH_HANDLER_HPP
