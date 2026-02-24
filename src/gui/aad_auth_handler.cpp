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

// Static flag for AAD debug logging
bool AADAuthHandler::s_aad_debug = false;

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
 * Rewrite the OAuth URL redirect URI only when needed.
 *
 * - Keep nativeclient redirects as-is (must match app registration and can be
 *   observed in navigation callbacks before the final wrongplace redirect).
 * - Keep existing localhost redirects as-is.
 * - Rewrite unsupported schemes like ms-appx-web:// to localhost callback.
 *
 * @param auth_url The original OAuth URL
 * @param port The local port to use for localhost callback rewrites
 * @return The modified OAuth URL
 */
static std::string rewrite_auth_url_with_localhost_redirect(const std::string& auth_url, uint16_t port) {
    std::string original_redirect = extract_redirect_uri(auth_url);
    if (original_redirect.empty()) {
        return auth_url;  // No redirect_uri found, return as-is
    }
    
    // Already using localhost? No rewrite needed.
    if (original_redirect.find("http://localhost") == 0 ||
        original_redirect.find("http://127.0.0.1") == 0) {
        return auth_url;
    }
    
    // Keep nativeclient redirect as-is to avoid AADSTS50011 redirect URI mismatch.
    if (original_redirect.find("/oauth2/nativeclient") != std::string::npos) {
        return auth_url;
    }

    // Rewrite unsupported redirect schemes (e.g. ms-appx-web://) to localhost callback.
    // This allows us to capture the OAuth code on Linux where ms-appx-web is unsupported.
#ifdef WEBUI_TLS
    std::string new_redirect = "https://localhost:" + std::to_string(port) + "/oauth/callback";
#else
    std::string new_redirect = "http://localhost:" + std::to_string(port) + "/oauth/callback";
#endif
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

void AADAuthHandler::enable_debug() {
    s_aad_debug = true;
    std::cout << "[AAD] Debug logging enabled (--aad-dbg)" << std::endl;
}

bool AADAuthHandler::is_debug_enabled() {
    return s_aad_debug;
}

void AADAuthHandler::log_url_details(const std::string& url, const std::string& context) {
    std::cout << "[AAD-DBG] --- " << context << " ---" << std::endl;
    std::cout << "[AAD-DBG] Full URL: " << url << std::endl;

    // Parse scheme and host
    size_t scheme_end = url.find("://");
    if (scheme_end != std::string::npos) {
        std::cout << "[AAD-DBG] Scheme: " << url.substr(0, scheme_end) << std::endl;
        size_t host_start = scheme_end + 3;
        size_t path_start = url.find('/', host_start);
        size_t query_start = url.find('?', host_start);
        size_t host_end = std::min(path_start, query_start);
        if (host_end != std::string::npos) {
            std::cout << "[AAD-DBG] Host: " << url.substr(host_start, host_end - host_start) << std::endl;
            if (path_start != std::string::npos && path_start < query_start) {
                size_t pend = (query_start != std::string::npos) ? query_start : url.size();
                std::cout << "[AAD-DBG] Path: " << url.substr(path_start, pend - path_start) << std::endl;
            }
        } else {
            std::cout << "[AAD-DBG] Host: " << url.substr(host_start) << std::endl;
        }
    }

    // Parse query parameters
    size_t qpos = url.find('?');
    if (qpos != std::string::npos) {
        std::string query = url.substr(qpos + 1);
        // Strip fragment
        size_t frag = query.find('#');
        if (frag != std::string::npos) query = query.substr(0, frag);

        std::cout << "[AAD-DBG] Query parameters:" << std::endl;
        std::istringstream qs(query);
        std::string param;
        while (std::getline(qs, param, '&')) {
            size_t eq = param.find('=');
            if (eq != std::string::npos) {
                std::string key = param.substr(0, eq);
                std::string val = url_decode(param.substr(eq + 1));
                // Mask sensitive values
                if (key == "code" || key == "client_secret") {
                    std::cout << "[AAD-DBG]   " << key << " = " << val.substr(0, 8) << "..." << std::endl;
                } else {
                    std::cout << "[AAD-DBG]   " << key << " = " << val << std::endl;
                }
            } else {
                std::cout << "[AAD-DBG]   " << param << std::endl;
            }
        }
    }

    // Parse fragment
    size_t fpos = url.find('#');
    if (fpos != std::string::npos) {
        std::cout << "[AAD-DBG] Fragment: " << url.substr(fpos + 1) << std::endl;
    }
    std::cout << "[AAD-DBG] --- end " << context << " ---" << std::endl;
}

AADAuthHandler::~AADAuthHandler() {
    // Clean up any pending window
    {
        std::lock_guard<std::mutex> lock(m_window_mutex);
        if (m_window) {
            m_window->close();
            // In destructor we can afford to wait synchronously
            std::this_thread::sleep_for(std::chrono::seconds(3));
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
    m_result = {success, redirect_url, m_actual_redirect_uri};
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
        // If process_navigation() completed the auth flow, signal that the
        // callback has fully returned so handle_authenticate() can safely
        // destroy the window without a use-after-free on this call stack.
        if (!s_instance->m_pending.load()) {
            s_instance->m_callback_complete.store(true);
        }
    } else if (event_type == webui::DISCONNECTED) {
        s_instance->handle_disconnected();
        if (!s_instance->m_pending.load()) {
            s_instance->m_callback_complete.store(true);
        }
    }
}

void AADAuthHandler::s_handle_oauth_callback(webui::window::event* e) {
    if (!s_instance) return;
    
    std::string url = e->get_string(0);
    std::cout << "[AAD] ============================================" << std::endl;
    std::cout << "[AAD] OAuth callback received via JavaScript" << std::endl;
    std::cout << "[AAD] Callback URL: " << url << std::endl;
    std::cout << "[AAD] ============================================" << std::endl;
    if (s_aad_debug) {
        log_url_details(url, "OAuth callback (JS)");
    }
    s_instance->process_navigation(url);
}

// Static handler for browser console.log forwarding
void AADAuthHandler::s_handle_log_to_backend(webui::window::event* e) {
    std::string message = e->get_string(0);
    // Always print JS-forwarded messages (they carry [AAD-Browser] prefix from the injected script)
    std::cout << message << std::endl;
}

// ============================================================================
// Navigation and event processing
// ============================================================================

void AADAuthHandler::process_navigation(const std::string& url) {
    std::cerr << "[AAD] NAVIGATION: " << url << std::endl;

    // When --aad-dbg is active, parse and display URL components
    if (s_aad_debug) {
        log_url_details(url, "Navigation");
    }
    
    // Check if this is the nativeclient redirect callback URL.
    // IMPORTANT: don't just search for "nativeclient" anywhere in the URL,
    // because authorize URLs include it inside the redirect_uri query parameter.
    // We only treat it as callback when the navigation path itself is /oauth2/nativeclient.
    bool is_nativeclient_callback = (
        (url.rfind("https://login.microsoftonline.com/", 0) == 0 ||
         url.rfind("http://login.microsoftonline.com/", 0) == 0) &&
        url.find("/oauth2/nativeclient") != std::string::npos);

    const bool has_auth_code = (url.find("code=") != std::string::npos);
    const bool has_auth_error = (url.find("error=") != std::string::npos);
    
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
    if (is_localhost_callback || is_nativeclient_callback || is_msappx_redirect || has_auth_code || has_auth_error) {
        
        std::cerr << "[AAD-DIAG] Auth code/error URL detected, acquiring m_mutex..." << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        std::cerr << "[AAD-DIAG] m_mutex acquired in process_navigation" << std::endl;
        
        if (has_auth_code) {
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
            
            m_result = {true, result_url, m_actual_redirect_uri};
        } else if (has_auth_error) {
            std::cout << "[AAD] ============================================" << std::endl;
            std::cout << "[AAD] AUTHENTICATION ERROR" << std::endl;
            std::cout << "[AAD] ============================================" << std::endl;
            std::cout << "[AAD] Error URL: " << url << std::endl;
            std::cout << "[AAD] ============================================" << std::endl;
            m_result = {false, "", ""};
        } else {
            // Intermediate redirect without code/error yet.
            // Do not fail/cancel here; continue waiting for the actual callback.
            std::cout << "[AAD] Redirect/navigation without code or error yet, continuing flow" << std::endl;
            std::cout << "[AAD] URL: " << url << std::endl;
            return;
        }
        
        m_pending = false;
        m_cv.notify_all();
        std::cerr << "[AAD-DIAG] process_navigation: cv.notify_all() called, m_pending=false" << std::endl;
        
        // Don't close the window here — it will be cleaned up by
        // handle_authenticate() after m_cv.wait_for() returns.
        // Calling close() from the WebUI callback thread can crash GTK.
    }
}

void AADAuthHandler::handle_disconnected() {
    std::cout << "[AAD] DISCONNECTED event received" << std::endl;
    // Check if we're navigating to OAuth - disconnect is expected in that case
    if (m_navigating_to_oauth) {
        std::cout << "[AAD] Expected disconnect during OAuth flow - waiting for callback" << std::endl;
        // Don't cancel the flow - we're waiting for the OAuth callback
        return;
    }
    
    std::cout << "[AAD] Window closed by user" << std::endl;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending) {
        m_result = {false, "", ""};
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
    m_callback_complete = false;     // Reset completion barrier
    m_result = {false, "", ""};
    
    std::cout << "[AAD] ============================================" << std::endl;
    std::cout << "[AAD] AUTHENTICATION REQUESTED" << std::endl;
    std::cout << "[AAD] ============================================" << std::endl;
    std::cout << "[AAD] Type: " << (request.type == AADAuthRequest::RDS_AAD ? "RDS_AAD" : "AVD") << std::endl;
    std::cout << "[AAD] Auth URL: " << request.auth_url << std::endl;
    
    // Extract and store the original redirect URI for later reconstruction
    m_original_redirect_uri = extract_redirect_uri(request.auth_url);
    std::cout << "[AAD] Original redirect URI: " << m_original_redirect_uri << std::endl;
    std::cout << "[AAD] ============================================" << std::endl;

    // Verbose debug: parse and display all auth URL parameters
    if (s_aad_debug) {
        log_url_details(request.auth_url, "Auth request URL");
    }
    
    // If manual code flow is enabled, print URL to console and wait for user input
    if (s_manual_code_flow) {
        return handle_manual_code_flow(request, lock);
    }
    
    std::cout << "[AAD] Opening native window..." << std::endl;
    
    // Prevent WebUI from exiting the GTK main loop when the AAD window
    // disconnects (navigating to login.microsoftonline.com drops the
    // WebSocket).  With timeout 0 the server-thread exit path skips the
    // "break main loop" signal, keeping the main window alive.
    webui::set_timeout(0);
    
    // Notify the main window about AAD auth (for UI feedback)
    if (m_main_window) {
        std::string type_str = (request.type == AADAuthRequest::RDS_AAD) ? "RDS_AAD" : "AVD";
        std::string js = "showToast('Azure AD authentication required (" + type_str + "). Opening login window...', 'info', 5000);";
        m_main_window->run(js);
    }
    
    // Create and configure the authentication window
    // Use WebView (native embedded browser) for a cleaner popup experience
    {
        std::lock_guard<std::mutex> win_lock(m_window_mutex);
        m_window = std::make_unique<webui::window>();
        
        // Set a reasonable size for the login popup
        m_window->set_size(800, 700);
        
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
            
            // Use native WebView for the auth popup
            bool shown = m_window->show(placeholder_html);
            
            if (!shown) {
                std::cerr << "[AAD] Failed to show auth window" << std::endl;
                m_result = {false, "", ""};
                m_pending = false;
                return m_result;
            }
            
            // Get the port now that the server is running
            port = m_window->get_port();
            std::cout << "[AAD] Auth callback server on port: " << port << std::endl;
            
            if (port == 0) {
                std::cerr << "[AAD] Failed to get server port" << std::endl;
                m_result = {false, "", ""};
                m_pending = false;
                return m_result;
            }
            
            // Rewrite redirect_uri only when required (e.g. ms-appx-web://).
            modified_auth_url = rewrite_auth_url_with_localhost_redirect(
                request.auth_url, static_cast<uint16_t>(port));
            m_actual_redirect_uri = extract_redirect_uri(modified_auth_url);
            if (m_actual_redirect_uri.empty()) {
                m_actual_redirect_uri = m_original_redirect_uri;
            }
            
            std::cout << "[AAD] Using redirect URI: " << m_actual_redirect_uri << std::endl;
            if (s_aad_debug) {
                log_url_details(modified_auth_url, "Auth URL (effective redirect)");
            }
        }
    }
    
    // Wait a moment for the webview to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Navigate the webview to the OAuth URL.
    // We use navigate() which now calls webkit_web_view_load_uri()
    // directly on Linux, so it works for external HTTPS URLs without
    // restarting the civetweb server.
    {
        std::lock_guard<std::mutex> win_lock(m_window_mutex);
        if (m_window) {
            std::cout << "[AAD] Navigating webview to auth URL..." << std::endl;
            if (s_aad_debug) {
                std::cout << "[AAD-DBG] Using navigate() for auth URL" << std::endl;
            }
            // Set flag BEFORE navigation — the WebSocket will disconnect when the
            // webview leaves localhost, and we must not treat that as user cancellation.
            m_navigating_to_oauth = true;
            m_window->navigate(modified_auth_url);
        } else {
            std::cerr << "[AAD] Window closed before navigation" << std::endl;
            m_result = {false, "", ""};
            m_pending = false;
            return m_result;
        }
    }
        
    // Wait for the window to complete (with timeout for OAuth flow)
    std::cerr << "[AAD-DIAG] Entering cv.wait_for (releasing m_mutex)..." << std::endl;
    auto status = m_cv.wait_for(lock, AUTH_TIMEOUT, [this] {
        return !m_pending.load();
    });
    std::cerr << "[AAD-DIAG] cv.wait_for returned, status=" << status << std::endl;
    
    // Wait for the WebUI callback (s_handle_window_events) to fully return
    // before destroying the window. Without this, handle_authenticate() can
    // destroy the window while process_navigation() is still on the WebUI
    // callback thread's call stack, causing a use-after-free SEGFAULT.
    {
        int wait_count = 0;
        while (!m_callback_complete.load() && wait_count < 100) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            lock.lock();
            wait_count++;
        }
        std::cerr << "[AAD-DIAG] Completion barrier done after " << wait_count << " iterations" << std::endl;
        // Brief grace period for WebUI's internal event loop to finish
        // processing after the callback returned
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        lock.lock();
    }

    // Clean up the window: close it but do NOT call destroy().
    // webui_destroy() frees window memory while the server thread may still be
    // running, causing a use-after-free SEGFAULT on pthread_mutex_lock.
    // The small memory leak from not destroying is acceptable for auth windows.
    {
        std::lock_guard<std::mutex> win_lock(m_window_mutex);
        if (m_window) {
            std::cerr << "[AAD-DIAG] Closing auth window (no destroy to avoid SEGFAULT)..." << std::endl;
            m_window->close();
            // Intentionally leak the window object — destroy() is unsafe
            m_window.release();
        }
    }
    
    // Restore normal timeout so the app exits when the user closes the main window
    webui::set_timeout(15);
    
    if (!status) {
        std::cerr << "[AAD] Auth timed out" << std::endl;
        // Notify main window about timeout
        if (m_main_window) {
            m_main_window->run("onAADAuthComplete(false);");
        }
        return {false, "", ""};
    }
    
    // Notify main window about the result
    if (m_main_window) {
        std::string js = "onAADAuthComplete(" + std::string(m_result.success ? "true" : "false") + ");";
        m_main_window->run(js);
    }
    
    std::cerr << "[AAD-DIAG] handle_authenticate returning, success=" << m_result.success << std::endl;
    return m_result;
}

// ============================================================================
// Manual code flow handler
// ============================================================================

AADAuthResponse AADAuthHandler::handle_manual_code_flow(const AADAuthRequest& request,
                                                        std::unique_lock<std::mutex>& lock) {
    // This method is called with lock already held
    
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
    
    // Release the lock while waiting for user input (RAII-safe)
    lock.unlock();
    
    // Read the redirect URL from stdin
    std::string redirect_url;
    std::getline(std::cin, redirect_url);
    
    // Re-acquire the lock (RAII-safe — will be released by unique_lock dtor)
    lock.lock();
    
    // Trim whitespace
    size_t start = redirect_url.find_first_not_of(" \t\n\r");
    size_t end = redirect_url.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
        redirect_url = redirect_url.substr(start, end - start + 1);
    }
    
    if (redirect_url.empty()) {
        std::cout << "[AAD] No URL provided, authentication cancelled" << std::endl;
        m_result = {false, "", ""};
        m_pending = false;
        return m_result;
    }
    
    // Process the redirect URL
    if (redirect_url.find("code=") != std::string::npos) {
        std::cout << "[AAD] Authorization code received" << std::endl;
        if (s_aad_debug) {
            log_url_details(redirect_url, "Manual code flow redirect");
        }
        // In manual mode, the user navigated with the original redirect_uri
        m_result = {true, redirect_url, m_original_redirect_uri};
    } else if (redirect_url.find("error=") != std::string::npos) {
        std::cout << "[AAD] Error in redirect URL" << std::endl;
        if (s_aad_debug) {
            log_url_details(redirect_url, "Manual code flow error redirect");
        }
        m_result = {false, "", ""};
    } else {
        std::cout << "[AAD] Invalid redirect URL (no code= or error= parameter)" << std::endl;
        m_result = {false, "", ""};
    }
    
    m_pending = false;
    
    // Notify main window about the result
    if (m_main_window) {
        std::string js = "onAADAuthComplete(" + std::string(m_result.success ? "true" : "false") + ");";
        m_main_window->run(js);
    }
    
    return m_result;
}
