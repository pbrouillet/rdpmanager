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

#include "rdp_launcher.hpp"
#include "config_manager.hpp"

namespace fs = std::filesystem;

// Global instances
static std::unique_ptr<RDPLauncher> g_rdp_launcher;
static std::unique_ptr<ConfigManager> g_config_manager;

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
    params.clipboard = true;
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
    
    bool success = g_config_manager->save_connection(name, host, port, username, domain);
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
    webui::window main_window;
    
    // Configure window properties
    main_window.set_size(1200, 800);
    
    // Bind JavaScript functions
    main_window.bind("connectRDP", js_connect_rdp);
    main_window.bind("getConnections", js_get_connections);
    main_window.bind("saveConnection", js_save_connection);
    main_window.bind("deleteConnection", js_delete_connection);
    main_window.bind("getAppInfo", js_get_app_info);
    
    // Load the HTML file
    fs::path html_path = ui_path / "index.html";
    std::cout << "[FRIDAY] Loading UI from: " << html_path << std::endl;
    
    if (!main_window.show_browser(html_path.string(), AnyBrowser)) {
        std::cerr << "[ERROR] Failed to open browser window" << std::endl;
        return 1;
    }
    
    std::cout << "[FRIDAY] Systems online. Awaiting your command, Boss." << std::endl;
    
    // Wait until the window is closed
    webui::wait();
    
    std::cout << "[FRIDAY] Shutting down. Goodbye, Boss." << std::endl;
    
    return 0;
}
