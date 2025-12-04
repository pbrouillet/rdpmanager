/**
 * Main Window - Encapsulates the WebUI window and all its dependencies
 * 
 * This class manages the main application window, including the RDP launcher,
 * configuration manager, dialog manager, and AAD authentication handler.
 */

#ifndef MAIN_WINDOW_HPP
#define MAIN_WINDOW_HPP

#include <webui.hpp>
#include <memory>
#include <string>
#include <filesystem>

// Forward declarations
class RDPLauncher;
class ConfigManager;
class DialogManager;
class AADAuthHandler;
class JSHandlers;

namespace fs = std::filesystem;

/**
 * MainWindow encapsulates the WebUI window and all application components.
 * 
 * It manages the lifecycle of the RDP launcher, configuration manager,
 * dialog manager, AAD auth handler, and JavaScript bindings.
 */
class MainWindow {
public:
    /**
     * Construct a MainWindow
     * @param debug_port Chrome DevTools remote debugging port (0 to disable)
     */
    explicit MainWindow(int debug_port = 0);
    ~MainWindow();

    // Non-copyable, non-movable
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

    /**
     * Initialize the window and all dependencies
     * @return true on success, false on failure
     */
    bool initialize();

    /**
     * Show the window
     * @return true on success, false on failure
     */
    bool show();

    /**
     * Get the underlying WebUI window (for advanced use)
     */
    webui::window& get_webui_window() { return m_window; }

    /**
     * Get the RDP launcher
     */
    RDPLauncher& get_rdp_launcher() { return *m_rdp_launcher; }

    /**
     * Get the config manager
     */
    ConfigManager& get_config_manager() { return *m_config_manager; }

    /**
     * Get the dialog manager
     */
    DialogManager& get_dialog_manager() { return *m_dialog_manager; }

    /**
     * Get the AAD auth handler
     */
    AADAuthHandler& get_aad_auth_handler() { return *m_aad_auth_handler; }

private:
    /**
     * Find and set the UI path
     * @return true if UI path was found, false otherwise
     */
    bool find_and_set_ui_path();

    /**
     * Configure debug settings if debug port is specified
     */
    void configure_debug_settings();

    /**
     * Set up RDP callbacks for dialogs
     */
    void setup_rdp_callbacks();

    /**
     * Bind JavaScript handlers
     */
    void bind_js_handlers();

    /**
     * Bind the WebUI ready event
     */
    void bind_ready_event();

    // ========================================================================
    // Members
    // ========================================================================
    
    webui::window m_window;
    int m_debug_port;
    fs::path m_ui_path;
    std::string m_debug_param;

    // Managers (owned by this window)
    std::unique_ptr<RDPLauncher> m_rdp_launcher;
    std::unique_ptr<ConfigManager> m_config_manager;
    std::unique_ptr<DialogManager> m_dialog_manager;
    std::unique_ptr<AADAuthHandler> m_aad_auth_handler;
    std::unique_ptr<JSHandlers> m_js_handlers;
};

#endif // MAIN_WINDOW_HPP
