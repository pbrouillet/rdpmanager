/**
 * AAD Auth Handler - Implementation
 */

#include "aad_auth_handler.hpp"
#include <iostream>

// Static instance pointer
AADAuthHandler* AADAuthHandler::s_instance = nullptr;

AADAuthHandler::AADAuthHandler() {
    s_instance = this;
}

AADAuthHandler::~AADAuthHandler() {
    // Clean up any pending window
    {
        std::lock_guard<std::mutex> lock(m_window_mutex);
        if (m_window) {
            m_window->destroy();
            m_window.reset();
        }
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void AADAuthHandler::set_main_window(webui::window* window) {
    m_main_window = window;
}

AADAuthCallback AADAuthHandler::get_callback() {
    return [this](const AADAuthRequest& request) {
        return this->handle_authenticate(request);
    };
}

void AADAuthHandler::on_response(bool success, const std::string& redirect_url) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_result = {success, redirect_url};
    m_pending = false;
    m_cv.notify_all();
    
    std::cout << "[AAD] Auth response: " << (success ? "completed" : "cancelled") << std::endl;
}

// ============================================================================
// Static event handler for WebUI
// ============================================================================

void AADAuthHandler::s_handle_window_events(webui::window::event* e) {
    if (!s_instance) return;
    
    if (e->get_type() == webui::NAVIGATION) {
        std::string url = e->get_string(0);
        s_instance->process_navigation(url);
    } else if (e->get_type() == webui::DISCONNECTED) {
        s_instance->handle_disconnected();
    }
}

// ============================================================================
// Navigation and event processing
// ============================================================================

void AADAuthHandler::process_navigation(const std::string& url) {
    std::cout << "[AAD] Navigation: " << url.substr(0, 100) << "..." << std::endl;
    
    // Check if this is the ms-appx-web:// redirect URL
    // This URL scheme is Windows-only, so we intercept it before the browser handles it
    bool is_redirect = (url.find("ms-appx-web://") == 0 || 
                        url.find("ms-appx-web%3A") != std::string::npos);
    
    // Also check for code= or error= in any URL (for compatibility)
    if (is_redirect || url.find("code=") != std::string::npos || url.find("error=") != std::string::npos) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (url.find("code=") != std::string::npos) {
            std::cout << "[AAD] Received authorization code" << std::endl;
            m_result = {true, url};
        } else if (url.find("error=") != std::string::npos) {
            std::cout << "[AAD] Received error in URL" << std::endl;
            m_result = {false, ""};
        } else {
            // ms-appx-web:// redirect without code - this shouldn't happen but handle it
            std::cout << "[AAD] ms-appx-web redirect without code" << std::endl;
            m_result = {false, ""};
        }
        
        m_pending = false;
        m_cv.notify_all();
        
        // Close the window
        {
            std::lock_guard<std::mutex> win_lock(m_window_mutex);
            if (m_window) {
                m_window->close();
            }
        }
    }
}

void AADAuthHandler::handle_disconnected() {
    std::cout << "[AAD] Window closed by user" << std::endl;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending) {
        m_result = {false, ""};
        m_pending = false;
        m_cv.notify_all();
    }
}

// ============================================================================
// Main authentication handler
// ============================================================================

AADAuthResponse AADAuthHandler::handle_authenticate(const AADAuthRequest& request) {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    // Store the request and initialize state
    m_request = request;
    m_pending = true;
    m_result = {false, ""};
    
    std::cout << "[AAD] Authentication requested, opening native window..." << std::endl;
    std::cout << "[AAD] Auth URL: " << request.auth_url << std::endl;
    
    // Notify the main window about AAD auth (for UI feedback)
    if (m_main_window) {
        std::string type_str = (request.type == AADAuthRequest::RDS_AAD) ? "RDS_AAD" : "AVD";
        std::string js = "showToast('Azure AD authentication required (" + type_str + "). Opening login window...', 'info', 5000);";
        m_main_window->run(js);
    }
    
    // Create and configure the authentication window
    {
        std::lock_guard<std::mutex> win_lock(m_window_mutex);
        m_window = std::make_unique<webui::window>();
        
        // IMPORTANT: Enable multi-client mode BEFORE any other configuration
        // This allows external browsers (without WebUI cookies) to connect
        webui::set_config(multi_client, true);
        
        // Configure window properties
        m_window->set_size(600, 700);
        m_window->set_center();
        
        // Set public mode to allow access from any interface
        m_window->set_public(true);
        
        // Bind event handler to intercept the ms-appx-web:// redirect
        m_window->bind("", s_handle_window_events);
    }
    
    // Show the window with an HTML page that redirects to the auth URL
    {
        std::lock_guard<std::mutex> win_lock(m_window_mutex);
        if (m_window) {
            // Create a simple HTML page that redirects to the OAuth URL
            // We use JavaScript redirect to avoid shell escaping issues with & characters
            std::string redirect_html = 
                "<!DOCTYPE html><html><head>"
                "<meta charset='UTF-8'>"
                "<title>Azure AD Login</title>"
                "<style>"
                "body{font-family:system-ui,-apple-system,sans-serif;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;background:#1a1a2e;color:#eee;}"
                ".loading{text-align:center;}"
                ".spinner{width:50px;height:50px;border:4px solid #333;border-top-color:#4a9eff;border-radius:50%;animation:spin 1s linear infinite;margin:0 auto 20px;}"
                "@keyframes spin{to{transform:rotate(360deg);}}"
                "</style>"
                "</head><body>"
                "<div class='loading'>"
                "<div class='spinner'></div>"
                "<p>Redirecting to Azure AD login...</p>"
                "</div>"
                "<script>"
                "setTimeout(function(){window.location.href='" + request.auth_url + "';},100);"
                "</script>"
                "</body></html>";
            
            std::cout << "[AAD] Showing auth window with native WebView" << std::endl;
            // Use show_wv() for native WebView - this allows intercepting navigation
            if (!m_window->show_wv(redirect_html)) {
                std::cerr << "[AAD] WebView not available, falling back to browser" << std::endl;
                m_window->show_browser(redirect_html, AnyBrowser);
            }
        }
    }
    
    // Wait for the window to complete (with timeout for OAuth flow)
    auto status = m_cv.wait_for(lock, AUTH_TIMEOUT, [this] {
        return !m_pending.load();
    });
    
    // Clean up the window
    {
        std::lock_guard<std::mutex> win_lock(m_window_mutex);
        if (m_window) {
            m_window->destroy();
            m_window.reset();
        }
    }
    
    if (!status) {
        std::cerr << "[AAD] Auth timed out" << std::endl;
        // Notify main window about timeout
        if (m_main_window) {
            m_main_window->run("onAADAuthComplete(false);");
        }
        return {false, ""};
    }
    
    // Notify main window about the result
    if (m_main_window) {
        std::string js = "onAADAuthComplete(" + std::string(m_result.success ? "true" : "false") + ");";
        m_main_window->run(js);
    }
    
    return m_result;
}
