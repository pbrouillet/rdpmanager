/**
 * Dialog Manager - Handles UI dialogs for RDP authentication
 * 
 * This module manages the synchronization between FreeRDP callback threads
 * and the WebUI frontend for certificate verification and authentication dialogs.
 * 
 * AAD authentication is handled by the separate AADAuthHandler class.
 */

#ifndef DIALOG_MANAGER_HPP
#define DIALOG_MANAGER_HPP

#include <webui.hpp>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <iostream>
#include <chrono>

#include "rdp_launcher.hpp"

/**
 * DialogManager handles certificate and authentication dialogs between FreeRDP and the UI.
 * 
 * It provides callbacks that can be registered with RDPLauncher, and those
 * callbacks will show dialogs in the WebUI frontend and wait for user response.
 * 
 * Note: AAD authentication is handled separately by AADAuthHandler.
 */
class DialogManager {
public:
    DialogManager() = default;
    ~DialogManager() = default;

    // Non-copyable, non-movable (due to mutexes and atomic state)
    DialogManager(const DialogManager&) = delete;
    DialogManager& operator=(const DialogManager&) = delete;
    DialogManager(DialogManager&&) = delete;
    DialogManager& operator=(DialogManager&&) = delete;

    /**
     * Set the main window for displaying dialogs
     * Must be called before any callbacks are invoked
     */
    void set_main_window(webui::window* window);

    /**
     * Get callback functions to register with RDPLauncher
     */
    CertificateVerifyCallback get_certificate_callback();
    AuthenticateCallback get_authenticate_callback();

    // ========================================================================
    // JavaScript response handlers - call these from JS bindings
    // ========================================================================

    /**
     * Handle certificate dialog response from JavaScript
     * @param choice 0=reject, 1=accept permanent, 2=accept temporary
     */
    void on_certificate_response(int choice);

    /**
     * Handle auth dialog response from JavaScript
     * @param success Whether user submitted (true) or cancelled (false)
     * @param username The entered username
     * @param password The entered password
     * @param domain The entered domain
     */
    void on_auth_response(bool success, const std::string& username,
                          const std::string& password, const std::string& domain);

private:
    // ========================================================================
    // Callback implementations - called from FreeRDP thread
    // ========================================================================
    
    CertificateAcceptance handle_certificate_verify(const CertificateInfo& info);
    AuthResponse handle_authenticate(const AuthRequest& request);

    // ========================================================================
    // State and synchronization
    // ========================================================================
    
    webui::window* m_main_window = nullptr;
    
    std::mutex m_dialog_mutex;
    std::condition_variable m_dialog_cv;

    // Certificate dialog state
    std::atomic<bool> m_cert_dialog_pending{false};
    CertificateInfo m_pending_cert_info;
    CertificateAcceptance m_cert_dialog_result{CertificateAcceptance::Reject};

    // Auth dialog state  
    std::atomic<bool> m_auth_dialog_pending{false};
    AuthRequest m_pending_auth_request;
    AuthResponse m_auth_dialog_result;
};

#endif // DIALOG_MANAGER_HPP
