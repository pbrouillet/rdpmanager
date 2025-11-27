/**
 * WebUI RDP Client - Main Application
 * 
 * A native C++ application using WebUI for the frontend
 * and FreeRDP for Remote Desktop connections.
 */

#include <webui.hpp>
#include <iostream>
#include <filesystem>
#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>

#include "rdp_launcher.hpp"
#include "config_manager.hpp"

namespace fs = std::filesystem;

// Global instances
static std::unique_ptr<RDPLauncher> g_rdp_launcher;
static std::unique_ptr<ConfigManager> g_config_manager;
static webui::window* g_main_window = nullptr;

// ============================================================================
// Dialog synchronization mechanism
// We need to show dialogs from the UI thread and wait for the response
// ============================================================================
static std::mutex g_dialog_mutex;
static std::condition_variable g_dialog_cv;

// Certificate dialog state
static std::atomic<bool> g_cert_dialog_pending{false};
static CertificateInfo g_pending_cert_info;
static CertificateAcceptance g_cert_dialog_result{CertificateAcceptance::Reject};

// Auth dialog state  
static std::atomic<bool> g_auth_dialog_pending{false};
static AuthRequest g_pending_auth_request;
static AuthResponse g_auth_dialog_result;

// AAD authentication dialog state
static std::atomic<bool> g_aad_dialog_pending{false};
static AADAuthRequest g_pending_aad_request;
static AADAuthResponse g_aad_dialog_result;

/**
 * Certificate verification callback - called from FreeRDP thread
 * Shows a dialog in the UI and waits for user response
 */
CertificateAcceptance handle_certificate_verify(const CertificateInfo& info) {
    std::unique_lock<std::mutex> lock(g_dialog_mutex);
    
    // Store the certificate info and signal the UI
    g_pending_cert_info = info;
    g_cert_dialog_pending = true;
    g_cert_dialog_result = CertificateAcceptance::Reject;
    
    // Call JavaScript to show the certificate dialog
    if (g_main_window) {
        std::string js = "showCertificateDialog(" +
            std::string("{") +
            "\"host\":\"" + info.host + "\"," +
            "\"port\":" + std::to_string(info.port) + "," +
            "\"commonName\":\"" + info.common_name + "\"," +
            "\"subject\":\"" + info.subject + "\"," +
            "\"issuer\":\"" + info.issuer + "\"," +
            "\"fingerprint\":\"" + info.fingerprint + "\"," +
            "\"isChanged\":" + (info.is_changed ? "true" : "false") + "," +
            "\"oldFingerprint\":\"" + info.old_fingerprint + "\"" +
            "});";
        g_main_window->run(js);
    }
    
    // Wait for the UI to respond (with timeout)
    auto status = g_dialog_cv.wait_for(lock, std::chrono::seconds(120), [] {
        return !g_cert_dialog_pending.load();
    });
    
    if (!status) {
        std::cerr << "[FRIDAY] Certificate dialog timed out" << std::endl;
        return CertificateAcceptance::Reject;
    }
    
    return g_cert_dialog_result;
}

/**
 * Authentication callback - called from FreeRDP thread
 * Shows a dialog in the UI and waits for user response
 */
AuthResponse handle_authenticate(const AuthRequest& request) {
    std::unique_lock<std::mutex> lock(g_dialog_mutex);
    
    // Store the request and signal the UI
    g_pending_auth_request = request;
    g_auth_dialog_pending = true;
    g_auth_dialog_result = {false, "", "", ""};
    
    // Call JavaScript to show the auth dialog
    if (g_main_window) {
        std::string js = "showAuthDialog(" +
            std::string("{") +
            "\"hostname\":\"" + request.hostname + "\"," +
            "\"isGateway\":" + (request.is_gateway ? "true" : "false") + "," +
            "\"currentUsername\":\"" + request.current_username + "\"," +
            "\"currentDomain\":\"" + request.current_domain + "\"" +
            "});";
        g_main_window->run(js);
    }
    
    // Wait for the UI to respond (with timeout)
    auto status = g_dialog_cv.wait_for(lock, std::chrono::seconds(120), [] {
        return !g_auth_dialog_pending.load();
    });
    
    if (!status) {
        std::cerr << "[FRIDAY] Auth dialog timed out" << std::endl;
        return {false, "", "", ""};
    }
    
    return g_auth_dialog_result;
}

/**
 * AAD Authentication callback - called from FreeRDP thread
 * Shows a browser/dialog in the UI for OAuth2 code flow and waits for redirect URL
 */
AADAuthResponse handle_aad_authenticate(const AADAuthRequest& request) {
    std::unique_lock<std::mutex> lock(g_dialog_mutex);
    
    // Store the request and signal the UI
    g_pending_aad_request = request;
    g_aad_dialog_pending = true;
    g_aad_dialog_result = {false, ""};
    
    // Call JavaScript to show the AAD auth dialog/browser
    if (g_main_window) {
        std::string type_str = (request.type == AADAuthRequest::RDS_AAD) ? "RDS_AAD" : "AVD";
        std::string js = "showAADAuthDialog(" +
            std::string("{") +
            "\"type\":\"" + type_str + "\"," +
            "\"authUrl\":\"" + request.auth_url + "\"," +
            "\"scope\":\"" + request.scope + "\"" +
            "});";
        g_main_window->run(js);
    }
    
    // Wait for the UI to respond (with longer timeout for OAuth flow)
    auto status = g_dialog_cv.wait_for(lock, std::chrono::seconds(300), [] {
        return !g_aad_dialog_pending.load();
    });
    
    if (!status) {
        std::cerr << "[FRIDAY] AAD auth dialog timed out" << std::endl;
        return {false, ""};
    }
    
    return g_aad_dialog_result;
}

/**
 * JavaScript binding: Respond to certificate dialog
 * Called from the UI when user makes a choice
 */
void js_certificate_response(webui::window::event* e) {
    int choice = e->get_int(0);  // 0=reject, 1=accept permanent, 2=accept temporary
    
    std::lock_guard<std::mutex> lock(g_dialog_mutex);
    g_cert_dialog_result = static_cast<CertificateAcceptance>(choice);
    g_cert_dialog_pending = false;
    g_dialog_cv.notify_all();
    
    std::cout << "[FRIDAY] Certificate response: " << choice << std::endl;
}

/**
 * JavaScript binding: Respond to auth dialog
 * Called from the UI when user submits credentials
 */
void js_auth_response(webui::window::event* e) {
    bool success = e->get_bool(0);
    std::string username = e->get_string(1);
    std::string password = e->get_string(2);
    std::string domain = e->get_string(3);
    
    std::lock_guard<std::mutex> lock(g_dialog_mutex);
    g_auth_dialog_result = {success, username, password, domain};
    g_auth_dialog_pending = false;
    g_dialog_cv.notify_all();
    
    std::cout << "[FRIDAY] Auth response: " << (success ? "submitted" : "cancelled") << std::endl;
}

/**
 * JavaScript binding: Respond to AAD authentication dialog
 * Called from the UI when OAuth flow completes (with redirect URL) or is cancelled
 */
void js_aad_auth_response(webui::window::event* e) {
    bool success = e->get_bool(0);
    std::string redirect_url = e->get_string(1);
    
    std::lock_guard<std::mutex> lock(g_dialog_mutex);
    g_aad_dialog_result = {success, redirect_url};
    g_aad_dialog_pending = false;
    g_dialog_cv.notify_all();
    
    std::cout << "[FRIDAY] AAD auth response: " << (success ? "completed" : "cancelled") << std::endl;
}

/**
 * JavaScript binding: Connect to RDP server
 * Called from the frontend when user clicks connect
 */
void js_connect_rdp(webui::window::event* e) {
    // Get connection parameters from JavaScript
    std::string host = e->get_string(0);
    int port = e->get_int(1);
    std::string username = e->get_string(2);
    std::string domain = e->get_string(3);
    
    // Advanced options
    bool home_drive = e->get_bool(4);
    bool clipboard = e->get_bool(5);
    bool cert_tofu = e->get_bool(6);
    bool usb_auto = e->get_bool(7);
    bool floatbar = e->get_bool(8);
    bool dynamic_resolution = e->get_bool(9);
    bool network_auto = e->get_bool(10);
    bool gfx_avc420 = e->get_bool(11);
    bool compression = e->get_bool(12);
    bool audio_pulse = e->get_bool(13);
    bool prevent_session_lock = e->get_bool(14);
    bool auto_reconnect = e->get_bool(15);
    int auto_reconnect_max_retries = e->get_int(16);
    
    std::cout << "[FRIDAY] Initiating RDP connection to " << host << ":" << port << std::endl;
    
    if (host.empty()) {
        e->return_string("{\"success\": false, \"error\": \"Host cannot be empty\"}");
        return;
    }
    
    RDPConnectionParams params;
    params.hostname = host;
    params.port = (port > 0) ? port : 3389;
    params.username = username;
    params.domain = domain;
    params.width = 1920;
    params.height = 1080;
    params.fullscreen = false;
    
    // Advanced features
    params.home_drive = home_drive;
    params.clipboard = clipboard;
    params.cert_tofu = cert_tofu;
    params.usb_auto = usb_auto;
    params.floatbar = floatbar;
    params.dynamic_resolution = dynamic_resolution;
    params.network_auto = network_auto;
    params.gfx_avc420 = gfx_avc420;
    params.compression = compression;
    params.audio_pulse = audio_pulse;
    params.prevent_session_lock = prevent_session_lock;
    params.auto_reconnect = auto_reconnect;
    params.auto_reconnect_max_retries = (auto_reconnect_max_retries > 0) ? auto_reconnect_max_retries : 3;
    
    // Legacy audio setting (keep for compatibility)
    params.audio = true;
    
    bool success = g_rdp_launcher->launch(params);
    
    if (success) {
        e->return_string("{\"success\": true}");
    } else {
        std::string error = g_rdp_launcher->get_last_error();
        e->return_string("{\"success\": false, \"error\": \"" + error + "\"}");
    }
}

/**
 * JavaScript binding: Get saved connections
 */
void js_get_connections(webui::window::event* e) {
    std::string json = g_config_manager->get_connections_json();
    e->return_string(json);
}

/**
 * JavaScript binding: Save a connection
 */
void js_save_connection(webui::window::event* e) {
    std::string name = e->get_string(0);
    std::string host = e->get_string(1);
    int port = e->get_int(2);
    std::string username = e->get_string(3);
    std::string domain = e->get_string(4);
    
    // Advanced options
    bool home_drive = e->get_bool(5);
    bool clipboard = e->get_bool(6);
    bool cert_tofu = e->get_bool(7);
    bool usb_auto = e->get_bool(8);
    bool floatbar = e->get_bool(9);
    bool dynamic_resolution = e->get_bool(10);
    bool network_auto = e->get_bool(11);
    bool gfx_avc420 = e->get_bool(12);
    bool compression = e->get_bool(13);
    bool audio_pulse = e->get_bool(14);
    bool prevent_session_lock = e->get_bool(15);
    bool auto_reconnect = e->get_bool(16);
    int auto_reconnect_max_retries = e->get_int(17);
    
    bool success = g_config_manager->save_connection(
        name, host, port, username, domain,
        home_drive, clipboard, cert_tofu, usb_auto, floatbar,
        dynamic_resolution, network_auto, gfx_avc420, compression,
        audio_pulse, prevent_session_lock, auto_reconnect,
        (auto_reconnect_max_retries > 0) ? auto_reconnect_max_retries : 3
    );
    e->return_bool(success);
}

/**
 * JavaScript binding: Delete a connection
 */
void js_delete_connection(webui::window::event* e) {
    std::string name = e->get_string(0);
    bool success = g_config_manager->delete_connection(name);
    e->return_bool(success);
}

/**
 * JavaScript binding: Get application info
 */
void js_get_app_info(webui::window::event* e) {
    std::string info = R"({
        "name": "WebUI RDP Client",
        "version": "0.1.0",
        "freerdp_version": ")" + g_rdp_launcher->get_freerdp_version() + R"("
    })";
    e->return_string(info);
}

/**
 * Find the UI directory path
 */
fs::path find_ui_path() {
    // Check compile-time path first
    #ifdef UI_PATH
    fs::path compile_path(UI_PATH);
    if (fs::exists(compile_path / "index.html")) {
        return compile_path;
    }
    #endif
    
    // Check relative paths
    std::vector<fs::path> search_paths = {
        "src/ui",
        "../src/ui",
        "../../src/ui",
        "ui",
        "../ui",
    };
    
    for (const auto& path : search_paths) {
        if (fs::exists(path / "index.html")) {
            return fs::absolute(path);
        }
    }
    
    // Check executable directory
    #ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    fs::path exe_dir = fs::path(buffer).parent_path();
    #else
    fs::path exe_dir = fs::read_symlink("/proc/self/exe").parent_path();
    #endif
    
    if (fs::exists(exe_dir / "ui" / "index.html")) {
        return exe_dir / "ui";
    }
    
    return "";
}

int main(int argc, char* argv[]) {
    std::cout << "============================================" << std::endl;
    std::cout << "  WebUI RDP Client v0.1.0" << std::endl;
    std::cout << "  F.R.I.D.A.Y. Remote Desktop Interface" << std::endl;
    std::cout << "============================================" << std::endl;
    
    // Initialize managers
    g_rdp_launcher = std::make_unique<RDPLauncher>();
    g_config_manager = std::make_unique<ConfigManager>();
    
    // Find UI files
    fs::path ui_path = find_ui_path();
    if (ui_path.empty()) {
        std::cerr << "[ERROR] Could not find UI files!" << std::endl;
        std::cerr << "Expected location: src/ui/index.html" << std::endl;
        return 1;
    }
    
    std::cout << "[FRIDAY] UI path: " << ui_path << std::endl;
    
    // Create the main window
    //webui_set_timeout(0); // Wait forever (never timeout)
    webui::window main_window;
    g_main_window = &main_window;
    
    // Set up RDP callbacks for dialogs
    g_rdp_launcher->set_certificate_callback(handle_certificate_verify);
    g_rdp_launcher->set_authenticate_callback(handle_authenticate);
    g_rdp_launcher->set_aad_auth_callback(handle_aad_authenticate);
    
    // Configure window properties
    main_window.set_size(1200, 800);
    
    // Set the root folder for serving CSS, JS, and other files
    if (!main_window.set_root_folder(ui_path.string())) {
        std::cerr << "[ERROR] Failed to set root folder: " << ui_path << std::endl;
        return 1;
    }
    std::cout << "[FRIDAY] Root folder set to: " << ui_path << std::endl;
    
    // Bind JavaScript functions
    main_window.bind("connectRDP", js_connect_rdp);
    main_window.bind("getConnections", js_get_connections);
    main_window.bind("saveConnection", js_save_connection);
    main_window.bind("deleteConnection", js_delete_connection);
    main_window.bind("getAppInfo", js_get_app_info);
    
    // Bind dialog response functions
    main_window.bind("certificateResponse", js_certificate_response);
    main_window.bind("authResponse", js_auth_response);
    main_window.bind("aadAuthResponse", js_aad_auth_response);
    
    // Load the HTML file (use relative path since root folder is set)
    std::cout << "[FRIDAY] Loading UI: index.html from " << ui_path << std::endl;
    
    // if (!main_window.show_browser("index.html", AnyBrowser)) {
    //     std::cerr << "[ERROR] Failed to open browser window" << std::endl;
    //     return 1;
    // }

    main_window.show("index.html");

    std::cout << "[FRIDAY] Systems online. Awaiting your command, Boss." << std::endl;
    
    // Wait until the window is closed
    webui::wait();
    g_main_window = nullptr;
    
    std::cout << "[FRIDAY] Shutting down. Goodbye, Boss." << std::endl;
    
    return 0;
}
