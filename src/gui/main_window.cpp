/**
 * Main Window - Implementation
 */

#include "main_window.hpp"
#include "../rdp_launcher.hpp"
#include "../config_manager.hpp"
#include "../dialog_manager.hpp"
#include "aad_auth_handler.hpp"
#include "../js_handlers.hpp"
#include "../path_utils.hpp"

#include <iostream>

MainWindow::MainWindow(int debug_port)
    : m_debug_port(debug_port)
{
}

MainWindow::~MainWindow() = default;

bool MainWindow::initialize() {
    // Initialize managers
    m_rdp_launcher = std::make_unique<RDPLauncher>();
    m_config_manager = std::make_unique<ConfigManager>();
    m_dialog_manager = std::make_unique<DialogManager>();
    m_aad_auth_handler = std::make_unique<AADAuthHandler>();

    // Find UI files
    if (!find_and_set_ui_path()) {
        return false;
    }

    // Set up dialog managers with the main window
    m_dialog_manager->set_main_window(&m_window);
    m_aad_auth_handler->set_main_window(&m_window);

    // Configure debug settings if needed
    configure_debug_settings();

    // Set up RDP callbacks for dialogs
    setup_rdp_callbacks();

    // Configure window properties
    m_window.set_size(1200, 800);

    // Set the root folder for serving CSS, JS, and other files
    if (!m_window.set_root_folder(m_ui_path.string())) {
        std::cerr << "[ERROR] Failed to set root folder: " << m_ui_path << std::endl;
        return false;
    }
    std::cout << "[RDPMAN] Root folder set to: " << m_ui_path << std::endl;

    // Bind JavaScript handlers
    bind_js_handlers();

    // Bind the WebUI ready event
    bind_ready_event();

    return true;
}

bool MainWindow::show() {
    std::cout << "[RDPMAN] Loading UI: index.html from " << m_ui_path << std::endl;

    // Use browser mode when debugging (required for remote-debugging-port to work)
    // WebView doesn't support Chrome DevTools remote debugging
    if (m_debug_port > 0) {
        std::cout << "[RDPMAN] Using browser mode for debugging (Edge)" << std::endl;
        if (!m_window.show_browser("index.html", Edge)) {
            std::cerr << "[RDPMAN] Edge not available, trying any Chromium-based browser" << std::endl;
            return m_window.show_browser("index.html", ChromiumBased);
        }
        return true;
    } else {
        return m_window.show("index.html");
    }
}

bool MainWindow::find_and_set_ui_path() {
    m_ui_path = path_utils::find_ui_path();
    if (m_ui_path.empty()) {
        std::cerr << "[ERROR] Could not find UI files!" << std::endl;
        std::cerr << "Expected location: src/ui/index.html" << std::endl;
        return false;
    }
    std::cout << "[RDPMAN] UI path: " << m_ui_path << std::endl;
    return true;
}

void MainWindow::configure_debug_settings() {
    if (m_debug_port > 0) {
        // Create a temporary profile directory for debugging
        std::string debug_profile = "/tmp/webui-rdp-debug-profile";
        m_debug_param = "--no-first-run --disable-extensions --disable-background-mode "
                        "--disable-sync --allow-insecure-localhost "
                        "--user-data-dir=" + debug_profile + " "
                        "--remote-debugging-port=" + std::to_string(m_debug_port);
        m_window.set_custom_parameters(m_debug_param.data());
        std::cout << "[RDPMAN] Chrome DevTools debugging enabled on port " << m_debug_port << std::endl;
        std::cout << "[RDPMAN] Debug profile: " << debug_profile << std::endl;
        std::cout << "[RDPMAN] Connect Chrome DevTools at: edge://inspect or http://localhost:" 
                  << m_debug_port << std::endl;
    }
}

void MainWindow::setup_rdp_callbacks() {
    m_rdp_launcher->set_certificate_callback(m_dialog_manager->get_certificate_callback());
    m_rdp_launcher->set_authenticate_callback(m_dialog_manager->get_authenticate_callback());
    m_rdp_launcher->set_aad_auth_callback(m_aad_auth_handler->get_callback());
    m_rdp_launcher->set_token_cache_lookup_callback(
        [this](const std::string& hostname, const std::string& cache_kind) {
            return m_config_manager->get_cached_token(hostname, cache_kind);
        });
    m_rdp_launcher->set_token_cache_store_callback(
        [this](const std::string& hostname,
               const std::string& cache_kind,
               const std::string& token,
               int64_t expires_at_epoch) {
            return m_config_manager->set_cached_token(hostname, cache_kind, token, expires_at_epoch);
        });
}

void MainWindow::bind_js_handlers() {
    m_js_handlers = std::make_unique<JSHandlers>(
        *m_rdp_launcher, *m_config_manager, *m_dialog_manager, *m_aad_auth_handler);
    m_js_handlers->bind_all(m_window);
}

void MainWindow::bind_ready_event() {
    // Bind an empty element to capture all events including connection events
    m_window.bind("", [](webui::window::event* e) {
        if (e->get_type() == webui::CONNECTED) {
            std::cout << "[RDPMAN] WebUI connected, triggering initial data load" << std::endl;
            // Trigger JavaScript to load initial data now that connection is ready
            e->get_window().run("if (typeof onWebuiReady === 'function') onWebuiReady();");
        }
    });
}
