/**
 * Dialog Manager - Implementation
 * 
 * Handles certificate verification and authentication dialogs.
 * AAD authentication is handled separately by AADAuthHandler.
 */

#include "dialog_manager.hpp"
#include "json_utils.hpp"
#include "logger.hpp"

#include <format>

void DialogManager::set_main_window(webui::window* window) {
    m_main_window = window;
}

CertificateVerifyCallback DialogManager::get_certificate_callback() {
    return [this](const CertificateInfo& info) {
        return this->handle_certificate_verify(info);
    };
}

AuthenticateCallback DialogManager::get_authenticate_callback() {
    return [this](const AuthRequest& request) {
        return this->handle_authenticate(request);
    };
}

// ============================================================================
// JavaScript response handlers
// ============================================================================

void DialogManager::on_certificate_response(int choice) {
    std::lock_guard<std::mutex> lock(m_dialog_mutex);
    m_cert_dialog_result = static_cast<CertificateAcceptance>(choice);
    m_cert_dialog_pending = false;
    m_dialog_cv.notify_all();
    
    LOG_DEBUG("DialogMgr", "Certificate response: " << choice);
}

void DialogManager::on_auth_response(bool success, const std::string& username,
                                      const std::string& password, const std::string& domain) {
    std::lock_guard<std::mutex> lock(m_dialog_mutex);
    m_auth_dialog_result = {success, username, password, domain};
    m_auth_dialog_pending = false;
    m_dialog_cv.notify_all();
    
    LOG_DEBUG("DialogMgr", "Auth response: " << (success ? "submitted" : "cancelled"));
}

// ============================================================================
// Callback implementations - called from FreeRDP thread
// ============================================================================

CertificateAcceptance DialogManager::handle_certificate_verify(const CertificateInfo& info) {
    std::unique_lock<std::mutex> lock(m_dialog_mutex);
    
    // Store the certificate info and signal the UI
    m_pending_cert_info = info;
    m_cert_dialog_pending = true;
    m_cert_dialog_result = CertificateAcceptance::Reject;
    
    // Call JavaScript to show the certificate dialog
    if (m_main_window) {
        auto js = std::format(
            R"(showCertificateDialog({{"host":"{}","port":{},"commonName":"{}","subject":"{}","issuer":"{}","fingerprint":"{}","isChanged":{},"oldFingerprint":"{}"}}))",
            json_utils::escape_string(info.host), info.port,
            json_utils::escape_string(info.common_name),
            json_utils::escape_string(info.subject),
            json_utils::escape_string(info.issuer),
            json_utils::escape_string(info.fingerprint),
            info.is_changed ? "true" : "false",
            json_utils::escape_string(info.old_fingerprint)
        );
        m_main_window->run(js);
    }
    
    // Wait for the UI to respond (with timeout)
    auto status = m_dialog_cv.wait_for(lock, std::chrono::seconds(120), [this] {
        return !m_cert_dialog_pending.load();
    });
    
    if (!status) {
        LOG_ERROR("DialogMgr", "Certificate dialog timed out");
        return CertificateAcceptance::Reject;
    }
    
    return m_cert_dialog_result;
}

AuthResponse DialogManager::handle_authenticate(const AuthRequest& request) {
    std::unique_lock<std::mutex> lock(m_dialog_mutex);
    
    // Store the request and signal the UI
    m_pending_auth_request = request;
    m_auth_dialog_pending = true;
    m_auth_dialog_result = {false, "", "", ""};
    
    // Call JavaScript to show the auth dialog
    if (m_main_window) {
        auto js = std::format(
            R"(showAuthDialog({{"hostname":"{}","isGateway":{},"currentUsername":"{}","currentDomain":"{}"}}))",
            json_utils::escape_string(request.hostname),
            request.is_gateway ? "true" : "false",
            json_utils::escape_string(request.current_username),
            json_utils::escape_string(request.current_domain)
        );
        m_main_window->run(js);
    }
    
    // Wait for the UI to respond (with timeout)
    auto status = m_dialog_cv.wait_for(lock, std::chrono::seconds(120), [this] {
        return !m_auth_dialog_pending.load();
    });
    
    if (!status) {
        LOG_ERROR("DialogMgr", "Auth dialog timed out");
        return {false, "", "", ""};
    }
    
    return m_auth_dialog_result;
}
