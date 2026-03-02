/**
 * WebUI RDP Client - Main Application
 * 
 * A native C++ application using WebUI for the frontend
 * and FreeRDP for Remote Desktop connections.
 */

#include <webui.hpp>
#include <iostream>
#include <string>
#include <array>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "gui/main_window.hpp"
#include "gui/aad_auth_handler.hpp"

namespace {

#if defined(__linux__)
bool is_directory(const char* path) {
    struct stat st {};
    return (path != nullptr) && (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

bool has_tls_module(const std::string& module_dir) {
    const std::array<const char*, 2> modules = {
        "libgiognutls.so",
        "libgioopenssl.so",
    };

    for (const char* module_name : modules) {
        std::string module_path = module_dir + "/" + module_name;
        struct stat st {};
        if (stat(module_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            return true;
        }
    }

    return false;
}

bool env_path_contains(const char* env_value, const std::string& path) {
    if (!env_value || path.empty()) {
        return false;
    }

    const std::string all_paths(env_value);
    size_t start = 0;
    while (start <= all_paths.size()) {
        const size_t end = all_paths.find(':', start);
        const std::string token = all_paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (token == path) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

void configure_webview_tls_backend() {
    const std::array<const char*, 4> module_dirs = {
        "/usr/lib/x86_64-linux-gnu/gio/modules",
        "/usr/lib/aarch64-linux-gnu/gio/modules",
        "/usr/lib/gio/modules",
        "/usr/lib64/gio/modules",
    };

    std::vector<std::string> tls_dirs;
    tls_dirs.reserve(module_dirs.size());

    for (const char* module_dir : module_dirs) {
        if (is_directory(module_dir) && has_tls_module(module_dir)) {
            tls_dirs.emplace_back(module_dir);
        }
    }

    if (tls_dirs.empty()) {
        std::cerr << "[RDPMAN] WARNING: No GIO TLS backend module found. "
                     "Install glib-networking to enable HTTPS in embedded WebView." << std::endl;
        return;
    }

    const char* current_extra = std::getenv("GIO_EXTRA_MODULES");
    std::string merged_extra = current_extra ? current_extra : "";
    bool updated_extra = false;

    for (const std::string& path : tls_dirs) {
        if (!env_path_contains(merged_extra.c_str(), path)) {
            if (!merged_extra.empty()) {
                merged_extra += ":";
            }
            merged_extra += path;
            updated_extra = true;
        }
    }

    if (updated_extra) {
        setenv("GIO_EXTRA_MODULES", merged_extra.c_str(), 1);
        std::cout << "[RDPMAN] GIO_EXTRA_MODULES=" << merged_extra << std::endl;
    }

    if (!std::getenv("GIO_MODULE_DIR")) {
        setenv("GIO_MODULE_DIR", tls_dirs.front().c_str(), 1);
        std::cout << "[RDPMAN] GIO_MODULE_DIR=" << tls_dirs.front() << std::endl;
    }
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    std::cout << "============================================" << std::endl;
    std::cout << "  WebUI RDP Client v0.1.0" << std::endl;
    std::cout << "  Remote Desktop Interface" << std::endl;
    std::cout << "============================================" << std::endl;

#if defined(__linux__)
    configure_webview_tls_backend();
#endif
    
    // Parse command line arguments
    int debug_port = 0;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--debug-port=") == 0) {
            try {
                debug_port = std::stoi(arg.substr(13));
            } catch (...) {
                std::cerr << "[RDPMAN] Invalid debug port: " << arg.substr(13) << std::endl;
            }
        } else if (arg == "-USE_MANUAL_CODE_FLOW") {
            // Enable manual AAD code flow mode
            AADAuthHandler::enable_manual_code_flow();
        } else if (arg == "--aad-dbg") {
            // Enable verbose AAD debug logging (navigations, headers, bodies)
            AADAuthHandler::enable_debug();
        }
    }
    
    // Create and initialize the main window
    MainWindow main_window(debug_port);
    
    if (!main_window.initialize()) {
        std::cerr << "[ERROR] Failed to initialize main window" << std::endl;
        return 1;
    }
    
    // Prevent the WebUI server from auto-exiting on WebSocket hiccups.
    // FreeRDP's X11 client calls XSetErrorHandler() and freerdp_handle_signals()
    // which replace global handlers and can cause brief WebSocket disruptions
    // in the GTK/WebKit webview.  With timeout 0 the server thread enters a
    // "wait forever" loop that only exits on an explicit webui::exit() call,
    // which we trigger from the main window's DISCONNECTED handler.
    webui::set_timeout(0);

    // Show the window
    if (!main_window.show()) {
        std::cerr << "[ERROR] Failed to show main window" << std::endl;
        return 1;
    }

    std::cout << "[RDPMAN] Systems online. Awaiting your command." << std::endl;
    
    // Wait until the window is closed
    webui::wait();
    
    std::cout << "[RDPMAN] Shutting down. Goodbye." << std::endl;
    
    return 0;
}
