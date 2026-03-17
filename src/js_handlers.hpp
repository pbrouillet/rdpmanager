/**
 * JavaScript Handlers - WebUI binding functions for the RDP client
 * 
 * These functions handle JavaScript calls from the frontend UI.
 * 
 * Note: WebUI requires plain function pointers for bindings, so we use
 * a global instance pointer to access the handlers.
 */

#pragma once

#include <webui.hpp>
#include <string>
#include <memory>

// Forward declarations
class RDPLauncher;
class ConfigManager;
class DialogManager;
class AADAuthHandler;
class FeedDiscoveryManager;

/**
 * JSHandlers encapsulates all JavaScript binding callback functions.
 * 
 * It holds references to the managers needed to service UI requests.
 * 
 * Note: Due to WebUI's C-style callback API, we need to use static methods
 * and a global instance pointer.
 */
class JSHandlers {
public:
    JSHandlers(RDPLauncher& rdp_launcher, 
               ConfigManager& config_manager,
               DialogManager& dialog_manager,
               AADAuthHandler& aad_auth_handler,
               FeedDiscoveryManager& feed_discovery);
    ~JSHandlers();

    /**
     * Register all JavaScript bindings with the given window
     */
    void bind_all(webui::window& window);

private:
    RDPLauncher& m_rdp_launcher;
    ConfigManager& m_config_manager;
    DialogManager& m_dialog_manager;
    AADAuthHandler& m_aad_auth_handler;
    FeedDiscoveryManager& m_feed_discovery;

    // ========================================================================
    // Static callbacks for WebUI (which requires plain function pointers)
    // ========================================================================
    static void s_connect_rdp(webui::window::event* e);
    static void s_get_connections(webui::window::event* e);
    static void s_save_connection(webui::window::event* e);
    static void s_delete_connection(webui::window::event* e);
    static void s_get_app_info(webui::window::event* e);
    static void s_import_rdp_file(webui::window::event* e);
    static void s_create_database(webui::window::event* e);
    static void s_create_database_dialog(webui::window::event* e);
    static void s_open_database(webui::window::event* e);
    static void s_open_database_dialog(webui::window::event* e);
    static void s_clone_database(webui::window::event* e);
    static void s_close_database(webui::window::event* e);
    static void s_get_database_status(webui::window::event* e);
    static void s_create_folder(webui::window::event* e);
    static void s_move_folder(webui::window::event* e);
    static void s_rename_folder(webui::window::event* e);
    static void s_delete_folder(webui::window::event* e);
    static void s_get_folders(webui::window::event* e);
    static void s_certificate_response(webui::window::event* e);
    static void s_auth_response(webui::window::event* e);
    static void s_aad_auth_response(webui::window::event* e);

    // Feed discovery handlers
    static void s_get_feed_accounts(webui::window::event* e);
    static void s_delete_feed_account(webui::window::event* e);
    static void s_discover_feeds(webui::window::event* e);
    static void s_log_off_account(webui::window::event* e);
    static void s_forget_account(webui::window::event* e);

    // Global instance pointer for static callbacks
    static JSHandlers* s_instance;
};


