/**
 * WebUI RDP Client - Main Application
 * 
 * A native C++ application using WebUI for the frontend
 * and FreeRDP for Remote Desktop connections.
 */

#include <webui.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <array>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "gui/main_window.hpp"
#include "gui/aad_auth_handler.hpp"
#include "logger.hpp"

namespace fs = std::filesystem;

namespace {

#if defined(__linux__)
bool is_directory(const char* path) {
    return path != nullptr && fs::is_directory(path);
}

bool has_tls_module(const std::string& module_dir) {
    constexpr std::array modules = {
        "libgiognutls.so",
        "libgioopenssl.so",
    };

    for (const auto* module_name : modules) {
        if (fs::is_regular_file(fs::path{module_dir} / module_name)) {
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
        LOG_WARN("RDPMAN", "No GIO TLS backend module found. "
                     "Install glib-networking to enable HTTPS in embedded WebView.");
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
        LOG_INFO("RDPMAN", "GIO_EXTRA_MODULES=" << merged_extra);
    }

    if (!std::getenv("GIO_MODULE_DIR")) {
        setenv("GIO_MODULE_DIR", tls_dirs.front().c_str(), 1);
        LOG_INFO("RDPMAN", "GIO_MODULE_DIR=" << tls_dirs.front());
    }
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    // Parse command line arguments (before logger init so --log-level takes effect)
    int debug_port = 0;
    auto console_level = logger::Level::Warn;
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg.starts_with("--debug-port=")) {
            try {
                debug_port = std::stoi(std::string{arg.substr(13)});
            } catch (...) {
                std::cerr << "Invalid debug port: " << arg.substr(13) << std::endl;
            }
        } else if (arg.starts_with("--log-level=")) {
            console_level = logger::parse_level(arg.substr(12));
        } else if (arg == "-USE_MANUAL_CODE_FLOW") {
            AADAuthHandler::enable_manual_code_flow();
        } else if (arg == "--aad-dbg") {
            AADAuthHandler::enable_debug();
            console_level = logger::Level::Debug;  // --aad-dbg implies debug console
        }
    }

    // Initialise logger: file always at Debug, console at requested level
    logger::Logger::instance().init(console_level);

    LOG_RAW("============================================\n");
    LOG_RAW("  WebUI RDP Client v0.1.0\n");
    LOG_RAW("  Remote Desktop Interface\n");
    LOG_RAW("============================================\n");

#if defined(__linux__)
    configure_webview_tls_backend();
#endif
    
    // Create and initialize the main window
    MainWindow main_window(debug_port);
    
    if (!main_window.initialize()) {
        LOG_ERROR("RDPMAN", "Failed to initialize main window");
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
        LOG_ERROR("RDPMAN", "Failed to show main window");
        return 1;
    }

    LOG_INFO("RDPMAN", "Systems online. Awaiting your command.");
    
    // Wait until the window is closed
    webui::wait();
    
    LOG_INFO("RDPMAN", "Shutting down. Goodbye.");
    
    return 0;
}
