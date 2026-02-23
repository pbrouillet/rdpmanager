/**
 * Dialog Manager - Implementation
 * 
 * Handles certificate verification and authentication dialogs.
 * AAD authentication is handled separately by AADAuthHandler.
 */

#include "dialog_manager.hpp"
#include "json_utils.hpp"

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
    
    std::cout << "[RDPMAN] Certificate response: " << choice << std::endl;
}

void DialogManager::on_auth_response(bool success, const std::string& username,
                                      const std::string& password, const std::string& domain) {
    std::lock_guard<std::mutex> lock(m_dialog_mutex);
    m_auth_dialog_result = {success, username, password, domain};
    m_auth_dialog_pending = false;
    m_dialog_cv.notify_all();
    
    std::cout << "[RDPMAN] Auth response: " << (success ? "submitted" : "cancelled") << std::endl;
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
        std::string js = "showCertificateDialog(" +
            std::string("{") +
            "\"host\":\"" + json_utils::escape_string(info.host) + "\"," +
            "\"port\":" + std::to_string(info.port) + "," +
            "\"commonName\":\"" + json_utils::escape_string(info.common_name) + "\"," +
            "\"subject\":\"" + json_utils::escape_string(info.subject) + "\"," +
            "\"issuer\":\"" + json_utils::escape_string(info.issuer) + "\"," +
            "\"fingerprint\":\"" + json_utils::escape_string(info.fingerprint) + "\"," +
            "\"isChanged\":" + (info.is_changed ? "true" : "false") + "," +
            "\"oldFingerprint\":\"" + json_utils::escape_string(info.old_fingerprint) + "\"" +
            "});";
        m_main_window->run(js);
    }
    
    // Wait for the UI to respond (with timeout)
    auto status = m_dialog_cv.wait_for(lock, std::chrono::seconds(120), [this] {
        return !m_cert_dialog_pending.load();
    });
    
    if (!status) {
        std::cerr << "[RDPMAN] Certificate dialog timed out" << std::endl;
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
        std::string js = "showAuthDialog(" +
            std::string("{") +
            "\"hostname\":\"" + json_utils::escape_string(request.hostname) + "\"," +
            "\"isGateway\":" + (request.is_gateway ? "true" : "false") + "," +
            "\"currentUsername\":\"" + json_utils::escape_string(request.current_username) + "\"," +
            "\"currentDomain\":\"" + json_utils::escape_string(request.current_domain) + "\"" +
            "});";
        m_main_window->run(js);
    }
    
    // Wait for the UI to respond (with timeout)
    auto status = m_dialog_cv.wait_for(lock, std::chrono::seconds(120), [this] {
        return !m_auth_dialog_pending.load();
    });
    
    if (!status) {
        std::cerr << "[RDPMAN] Auth dialog timed out" << std::endl;
        return {false, "", "", ""};
    }
    
    return m_auth_dialog_result;
}
