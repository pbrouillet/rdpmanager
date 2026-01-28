/**
 * AAD Auth Handler - Implementation
 */

#include "aad_auth_handler.hpp"
#include <iostream>
#include <regex>
#include <sstream>
#include <iomanip>
#include <thread>

// Static instance pointer
AADAuthHandler* AADAuthHandler::s_instance = nullptr;

// Static flag for manual code flow mode
bool AADAuthHandler::s_manual_code_flow = false;

// ============================================================================
// URL encoding/decoding helpers
// ============================================================================

static std::string url_decode(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int hex_val = 0;
            std::istringstream hex_stream(str.substr(i + 1, 2));
            hex_stream >> std::hex >> hex_val;
            result += static_cast<char>(hex_val);
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

static std::string url_encode(const std::string& str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;
    
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << std::uppercase;
            encoded << '%' << std::setw(2) << int(c);
            encoded << std::nouppercase;
        }
    }
    return encoded.str();
}

/**
 * Extract the redirect_uri parameter from an OAuth URL
 */
static std::string extract_redirect_uri(const std::string& auth_url) {
    // Look for redirect_uri= (URL encoded or not)
    std::regex redirect_regex("redirect_uri=([^&]+)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(auth_url, match, redirect_regex)) {
        return url_decode(match[1].str());
    }
    return "";
}

/**
 * Check if a redirect URI is the Microsoft nativeclient URI.
 * This is a standard HTTPS URL that doesn't require rewriting.
 */
static bool is_nativeclient_redirect(const std::string& redirect_uri) {
    return redirect_uri.find("login.microsoftonline.com") != std::string::npos &&
           redirect_uri.find("nativeclient") != std::string::npos;
}

/**
 * Rewrite the OAuth URL to use a localhost redirect URI instead of ms-appx-web://
 * This is necessary because WebKitGTK doesn't support custom URI schemes like ms-appx-web://
 * and will crash when trying to navigate to them.
 * 
 * Note: nativeclient redirect URIs (https://login.microsoftonline.com/common/oauth2/nativeclient)
 * are standard HTTPS URLs and don't need rewriting.
 * 
 * @param auth_url The original OAuth URL with ms-appx-web:// redirect URI
 * @param port The local port to use for the redirect
 * @return The modified OAuth URL with http://localhost redirect URI, or original if nativeclient
 */
static std::string rewrite_auth_url_with_localhost_redirect(const std::string& auth_url, uint16_t port) {
    std::string original_redirect = extract_redirect_uri(auth_url);
    if (original_redirect.empty()) {
        return auth_url;  // No redirect_uri found, return as-is
    }
    
    // nativeclient redirect is a standard HTTPS URL, no rewriting needed
    if (is_nativeclient_redirect(original_redirect)) {
        return auth_url;  // nativeclient URL, return as-is
    }
    
    // Only rewrite if it's an ms-appx-web:// URL
    if (original_redirect.find("ms-appx-web://") != 0) {
        return auth_url;  // Not an ms-appx-web URL, return as-is
    }
    
    // Create localhost redirect URI
    std::string new_redirect = "http://localhost:" + std::to_string(port) + "/oauth/callback";
    std::string new_redirect_encoded = url_encode(new_redirect);
    
    // Replace the redirect_uri in the URL
    std::string result = auth_url;
    std::regex redirect_regex("redirect_uri=[^&]+", std::regex::icase);
    result = std::regex_replace(result, redirect_regex, "redirect_uri=" + new_redirect_encoded);
    
    return result;
}

/**
 * Reconstruct the original redirect URL from a localhost callback.
 * This allows FreeRDP to process the authorization code correctly.
 * For nativeclient redirects, we return the URL as-is since it's already in the correct format.
 * 
 * @param callback_url The URL that was received (localhost or nativeclient)
 * @param original_redirect The original redirect URI (ms-appx-web:// or nativeclient)
 * @return The reconstructed URL with the query parameters
 */
static std::string reconstruct_original_redirect(const std::string& callback_url, 
                                                  const std::string& original_redirect) {
    // Extract query string from callback URL
    size_t query_pos = callback_url.find('?');
    if (query_pos == std::string::npos) {
        return original_redirect;
    }
    
    std::string query_string = callback_url.substr(query_pos);
    
    // Combine original redirect URI base with the query string
    return original_redirect + query_string;
}

AADAuthHandler::AADAuthHandler() {
    s_instance = this;
}

void AADAuthHandler::enable_manual_code_flow() {
    s_manual_code_flow = true;
    std::cout << "[AAD] Manual code flow mode enabled" << std::endl;
}

bool AADAuthHandler::is_manual_code_flow_enabled() {
    return s_manual_code_flow;
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
    
    auto event_type = e->get_type();
    if (event_type == webui::NAVIGATION) {
        std::string url = e->get_string(0);
        s_instance->process_navigation(url);
    } else if (event_type == webui::DISCONNECTED) {
        s_instance->handle_disconnected();
    }
}

void AADAuthHandler::s_handle_oauth_callback(webui::window::event* e) {
    if (!s_instance) return;
    
    std::string url = e->get_string(0);
    std::cout << "[AAD] ============================================" << std::endl;
    std::cout << "[AAD] OAuth callback received via JavaScript" << std::endl;
    std::cout << "[AAD] Callback URL: " << url << std::endl;
    std::cout << "[AAD] ============================================" << std::endl;
    s_instance->process_navigation(url);
}

// Static handler for browser console.log forwarding
void AADAuthHandler::s_handle_log_to_backend(webui::window::event* e) {
    std::string message = e->get_string(0);
    std::cout << message << std::endl;
}

// ============================================================================
// Navigation and event processing
// ============================================================================

void AADAuthHandler::process_navigation(const std::string& url) {
    std::cout << "[AAD] ============================================" << std::endl;
    std::cout << "[AAD] NAVIGATION EVENT" << std::endl;
    std::cout << "[AAD] ============================================" << std::endl;
    std::cout << "[AAD] URL: " << url << std::endl;
    std::cout << "[AAD] ============================================" << std::endl;
    
    // Check if this is the nativeclient redirect URL (contains both login.microsoftonline.com AND nativeclient)
    // This is the preferred redirect for AVD auth - it's a standard HTTPS URL
    // IMPORTANT: Check this BEFORE the OAuth provider check since nativeclient URLs also contain login.microsoftonline.com
    bool is_nativeclient_callback = (url.find("login.microsoftonline.com") != std::string::npos &&
                                      url.find("nativeclient") != std::string::npos);
    
    // Check if this is navigation TO the OAuth provider (Microsoft login)
    // We need to track this so we don't treat the disconnect as user cancellation
    // Skip this check if it's a nativeclient callback (which also contains login.microsoftonline.com)
    if (!is_nativeclient_callback &&
        (url.find("login.microsoftonline.com") != std::string::npos ||
         url.find("login.microsoft.com") != std::string::npos ||
         url.find("login.live.com") != std::string::npos)) {
        std::cout << "[AAD] Navigating to OAuth provider - expecting disconnect" << std::endl;
        m_navigating_to_oauth = true;
        return;  // Don't process further, let the browser navigate
    }
    
    // Check if this is our localhost OAuth callback
    // We use localhost redirect to avoid WebKit crashing on ms-appx-web:// scheme
    bool is_localhost_callback = (url.find("localhost:") != std::string::npos && 
                                   url.find("/oauth/callback") != std::string::npos);
    
    // Check if this is the ms-appx-web:// redirect URL (legacy)
    // This URL scheme is Windows-only, so we intercept it before the browser handles it
    bool is_msappx_redirect = (url.find("ms-appx-web://") == 0 || 
                               url.find("ms-appx-web%3A") != std::string::npos);
    
    // Also check for code= or error= in any URL (for compatibility)
    if (is_localhost_callback || is_nativeclient_callback || is_msappx_redirect || 
        url.find("code=") != std::string::npos || url.find("error=") != std::string::npos) {
        
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (url.find("code=") != std::string::npos) {
            std::cout << "[AAD] ============================================" << std::endl;
            std::cout << "[AAD] AUTHORIZATION CODE RECEIVED" << std::endl;
            std::cout << "[AAD] ============================================" << std::endl;
            std::cout << "[AAD] Callback URL: " << url << std::endl;
            
            // For nativeclient redirect, the URL is already in the correct format
            // For localhost callback, reconstruct the original redirect URL for FreeRDP
            std::string result_url = url;
            if (is_localhost_callback && !m_original_redirect_uri.empty()) {
                result_url = reconstruct_original_redirect(url, m_original_redirect_uri);
                std::cout << "[AAD] Reconstructed redirect URL: " << result_url << std::endl;
            } else if (is_nativeclient_callback) {
                std::cout << "[AAD] Using nativeclient redirect URL directly" << std::endl;
            }
            std::cout << "[AAD] ============================================" << std::endl;
            
            m_result = {true, result_url};
        } else if (url.find("error=") != std::string::npos) {
            std::cout << "[AAD] ============================================" << std::endl;
            std::cout << "[AAD] AUTHENTICATION ERROR" << std::endl;
            std::cout << "[AAD] ============================================" << std::endl;
            std::cout << "[AAD] Error URL: " << url << std::endl;
            std::cout << "[AAD] ============================================" << std::endl;
            m_result = {false, ""};
        } else {
            // Redirect URL without code - this shouldn't happen but handle it
            std::cout << "[AAD] WARNING: Redirect without code or error" << std::endl;
            std::cout << "[AAD] URL: " << url << std::endl;
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
    // Check if we're navigating to OAuth - disconnect is expected in that case
    if (m_navigating_to_oauth) {
        std::cout << "[AAD] Expected disconnect during OAuth flow - waiting for callback" << std::endl;
        // Don't cancel the flow - we're waiting for the OAuth callback
        return;
    }
    
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
    m_navigating_to_oauth = false;  // Reset navigation flag
    m_result = {false, ""};
    
    std::cout << "[AAD] ============================================" << std::endl;
    std::cout << "[AAD] AUTHENTICATION REQUESTED" << std::endl;
    std::cout << "[AAD] ============================================" << std::endl;
    std::cout << "[AAD] Type: " << (request.type == AADAuthRequest::RDS_AAD ? "RDS_AAD" : "AVD") << std::endl;
    std::cout << "[AAD] Auth URL: " << request.auth_url << std::endl;
    
    // Extract and store the original redirect URI for later reconstruction
    m_original_redirect_uri = extract_redirect_uri(request.auth_url);
    std::cout << "[AAD] Original redirect URI: " << m_original_redirect_uri << std::endl;
    std::cout << "[AAD] ============================================" << std::endl;
    
    // If manual code flow is enabled, print URL to console and wait for user input
    if (s_manual_code_flow) {
        return handle_manual_code_flow(request);
    }
    
    std::cout << "[AAD] Opening native window..." << std::endl;
    
    // Notify the main window about AAD auth (for UI feedback)
    if (m_main_window) {
        std::string type_str = (request.type == AADAuthRequest::RDS_AAD) ? "RDS_AAD" : "AVD";
        std::string js = "showToast('Azure AD authentication required (" + type_str + "). Opening login window...', 'info', 5000);";
        m_main_window->run(js);
    }
    
    // Create and configure the authentication window
    // We use a browser window instead of WebView to avoid WebKitGTK threading issues
    // (WebKitGTK crashes when called from non-main thread)
    {
        std::lock_guard<std::mutex> win_lock(m_window_mutex);
        m_window = std::make_unique<webui::window>();
        
        // IMPORTANT: Enable multi-client mode BEFORE any other configuration
        // This allows external browsers (without WebUI cookies) to connect
        webui::set_config(multi_client, true);
        
        // Set public mode to allow access from any interface
        m_window->set_public(true);
        
        // Bind event handler to intercept the OAuth redirect
        // Using empty string "" captures ALL window events (NAVIGATION, DISCONNECTED, etc.)
        m_window->bind("", s_handle_window_events);
        
        // Bind a function to handle the OAuth callback
        // This will be called from the callback page's JavaScript
        m_window->bind("oauthCallback", s_handle_oauth_callback);
        
        // Bind a function to forward browser console.log to backend
        m_window->bind("logToBackend", s_handle_log_to_backend);
    }
    
    // Get the window's port for the localhost redirect
    size_t port = 0;
    std::string modified_auth_url;
    
    // Show a window with a page that will redirect to the OAuth URL
    // This page serves as our callback endpoint too
    {
        std::lock_guard<std::mutex> win_lock(m_window_mutex);
        if (m_window) {
            std::cout << "[AAD] Setting up auth callback server..." << std::endl;
            
            // First, start the server by showing a placeholder page
            // We need to get the port before we can create the proper redirect URL
            // This page also serves as the callback handler - it checks for OAuth params
            std::string placeholder_html = 
                "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "<title>Azure AD Authentication</title>"
                "<script src=\"webui.js\"></script>"
                "<script>"
                "// Capture console.log and send to backend"
                "(function() {"
                "  var originalLog = console.log;"
                "  console.log = function() {"
                "    var msg = Array.prototype.slice.call(arguments).join(' ');"
                "    originalLog.apply(console, arguments);"
                "    if (typeof logToBackend !== 'undefined') {"
                "      logToBackend('[AAD-Browser] ' + msg);"
                "    }"
                "  };"
                "})();"
                ""
                "window.onload = function() {"
                "  var url = window.location.href;"
                "  console.log('AAD Auth window loaded');"
                "  console.log('Current URL: ' + url);"
                "  // Check if this is the OAuth callback (has code= or error=)"
                "  if (url.indexOf('code=') !== -1 || url.indexOf('error=') !== -1) {"
                "    console.log('OAuth callback detected!');"
                "    console.log('Callback URL: ' + url);"
                "    // Call the bound function to process the callback"
                "    if (typeof oauthCallback !== 'undefined') {"
                "      console.log('Calling oauthCallback...');"
                "      oauthCallback(url);"
                "    } else {"
                "      console.error('oauthCallback not defined yet');"
                "    }"
                "  } else {"
                "    console.log('No OAuth parameters in URL, waiting for redirect...');"
                "  }"
                "};"
                "</script>"
                "<style>"
                "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; "
                "       display: flex; flex-direction: column; align-items: center; justify-content: center; "
                "       height: 100vh; margin: 0; background: #f5f5f5; }"
                "h1 { color: #333; }"
                ".spinner { width: 50px; height: 50px; border: 5px solid #e0e0e0; "
                "           border-top: 5px solid #0078d4; border-radius: 50%; animation: spin 1s linear infinite; }"
                "@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }"
                "</style>"
                "</head>"
                "<body>"
                "<h1>Azure AD Authentication</h1>"
                "<div class=\"spinner\"></div>"
                "<p id=\"status\">Setting up authentication...</p>"
                "</body>"
                "</html>";
            
            // Use show_browser() to open in external browser - avoids WebKitGTK threading issues
            bool shown = m_window->show_browser(placeholder_html, AnyBrowser);
            
            if (!shown) {
                std::cerr << "[AAD] Failed to show auth window in browser" << std::endl;
                m_result = {false, ""};
                m_pending = false;
                return m_result;
            }
            
            // Get the port now that the server is running
            port = m_window->get_port();
            std::cout << "[AAD] Auth callback server on port: " << port << std::endl;
            
            if (port == 0) {
                std::cerr << "[AAD] Failed to get server port" << std::endl;
                m_result = {false, ""};
                m_pending = false;
                return m_result;
            }
            
            // Rewrite the auth URL with the actual port for the callback
            modified_auth_url = rewrite_auth_url_with_localhost_redirect(
                request.auth_url, static_cast<uint16_t>(port));
            
            std::cout << "[AAD] Modified Auth URL (localhost redirect on port " << port << "): " 
                      << modified_auth_url << "..." << std::endl;
        }
    }
    
    // Wait a moment for the browser to connect
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Navigate the browser to the actual OAuth URL
    {
        std::lock_guard<std::mutex> win_lock(m_window_mutex);
        if (m_window) {
            std::cout << "[AAD] Navigating browser to auth URL..." << std::endl;
            // Use JavaScript to redirect instead of navigate() to avoid WebKitGTK issues
            std::string redirect_js = "window.location.href = '" + modified_auth_url + "';";
            m_window->run(redirect_js);
        } else {
            std::cerr << "[AAD] Window closed before navigation" << std::endl;
            m_result = {false, ""};
            m_pending = false;
            return m_result;
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

// ============================================================================
// Manual code flow handler
// ============================================================================

AADAuthResponse AADAuthHandler::handle_manual_code_flow(const AADAuthRequest& request) {
    // This method is called with m_mutex already locked
    
    std::string type_str = (request.type == AADAuthRequest::RDS_AAD) ? "RDS_AAD" : "AVD";
    
    std::cout << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "  MANUAL AZURE AD AUTHENTICATION" << std::endl;
    std::cout << "  Type: " << type_str << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Please open the following URL in your browser:" << std::endl;
    std::cout << std::endl;
    std::cout << request.auth_url << std::endl;
    std::cout << std::endl;
    std::cout << "After completing authentication, you will be redirected to a URL" << std::endl;
    std::cout << "starting with: " << m_original_redirect_uri << std::endl;
    std::cout << std::endl;
    std::cout << "Copy the ENTIRE redirect URL (including the ?code=... or ?error=... part)" << std::endl;
    std::cout << "and paste it below, then press Enter:" << std::endl;
    std::cout << std::endl;
    std::cout << "> ";
    std::cout.flush();
    
    // Notify main window about manual auth (for UI feedback)
    if (m_main_window) {
        std::string js = "showToast('Azure AD authentication required (" + type_str + "). Check the console for manual authentication instructions.', 'info', 10000);";
        m_main_window->run(js);
    }
    
    // Release the lock while waiting for user input
    m_mutex.unlock();
    
    // Read the redirect URL from stdin
    std::string redirect_url;
    std::getline(std::cin, redirect_url);
    
    // Re-acquire the lock
    m_mutex.lock();
    
    // Trim whitespace
    size_t start = redirect_url.find_first_not_of(" \t\n\r");
    size_t end = redirect_url.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
        redirect_url = redirect_url.substr(start, end - start + 1);
    }
    
    if (redirect_url.empty()) {
        std::cout << "[AAD] No URL provided, authentication cancelled" << std::endl;
        m_result = {false, ""};
        m_pending = false;
        return m_result;
    }
    
    // Process the redirect URL
    if (redirect_url.find("code=") != std::string::npos) {
        std::cout << "[AAD] Authorization code received" << std::endl;
        m_result = {true, redirect_url};
    } else if (redirect_url.find("error=") != std::string::npos) {
        std::cout << "[AAD] Error in redirect URL" << std::endl;
        m_result = {false, ""};
    } else {
        std::cout << "[AAD] Invalid redirect URL (no code= or error= parameter)" << std::endl;
        m_result = {false, ""};
    }
    
    m_pending = false;
    
    // Notify main window about the result
    if (m_main_window) {
        std::string js = "onAADAuthComplete(" + std::string(m_result.success ? "true" : "false") + ");";
        m_main_window->run(js);
    }
    
    return m_result;
}
