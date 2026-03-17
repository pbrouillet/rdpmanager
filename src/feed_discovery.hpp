/**
 * Feed Discovery Manager - WVD/AVD Feed Discovery
 *
 * Manages the workflow of authenticating against the WVD webclient,
 * extracting the bearer token from sessionStorage, and using it to
 * discover tenant feeds, workspace resources, and import RDP entries.
 */

#pragma once

#include <webui.hpp>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <chrono>

// Forward declarations
class ConfigManager;

// ============================================================================
// Data structures for feed discovery
// ============================================================================

struct TenantFeed {
    std::string tenant_id;
    std::string tenant_display_name;
    std::string workspace_name;
    std::string feed_url;
    std::string geo;
    std::string arm_path;
    std::string hub_discovery_url;
};

struct FeedResource {
    std::string id;
    std::string title;
    std::string type;          // "RemoteApp" or "Desktop"
    std::string arm_path;
    std::string rdp_url;
    std::string icon_url;
    std::string publisher_name;
};

struct FeedDiscoveryResult {
    bool success = false;
    std::string error;
    int imported_count = 0;
    int tenant_count = 0;
    // Account info (populated after authentication)
    std::string account_id;
    std::string account_display_name;
};

// Progress callback: (message, current, total)
using FeedProgressCallback = std::function<void(const std::string&, int, int)>;

/**
 * FeedDiscoveryManager handles the complete WVD feed discovery workflow:
 * 1. Open a popup to the WVD webclient for authentication
 * 2. Poll sessionStorage for the WVD access token
 * 3. Use the token to discover tenant feeds
 * 4. Fetch resources from each tenant
 * 5. Download and import RDP files into the database
 */
class FeedDiscoveryManager {
public:
    FeedDiscoveryManager(ConfigManager& config_manager);
    ~FeedDiscoveryManager();

    // Non-copyable, non-movable
    FeedDiscoveryManager(const FeedDiscoveryManager&) = delete;
    FeedDiscoveryManager& operator=(const FeedDiscoveryManager&) = delete;

    /**
     * Set the main window (for JS notifications)
     */
    void set_main_window(webui::window* window);

    /**
     * Run the complete feed discovery flow:
     * authenticate -> discover -> import
     *
     * @param account_id Existing feed account ID to refresh, or empty string
     *                   to create a new account (identity inferred from JWT).
     * @return Result indicating success/failure, import count, and account info.
     *
     * Note: This opens a popup window and blocks until the user
     * authenticates and all feeds are imported.
     */
    FeedDiscoveryResult discover_and_import(const std::string& account_id = "");

    /**
     * Check if a discovery is currently in progress
     */
    [[nodiscard]] bool is_busy() const { return m_busy.load(); }

private:
    // ========================================================================
    // Authentication (popup-based)
    // ========================================================================

    /**
     * Open a popup for Microsoft login using PKCE OAuth flow.
     * Intercepts the auth code from the redirect, exchanges it for tokens.
     * Blocks until the user authenticates or timeout.
     * @return The bearer token, or empty string on failure.
     */
    std::string authenticate_popup();

    /**
     * Exchange an authorization code for tokens via PKCE token endpoint.
     * @return The access token, or empty string on failure.
     */
    std::string exchange_code_for_token(const std::string& code);

    /**
     * Static event handler for popup window events
     */
    static void s_handle_popup_events(webui::window::event* e);

    /**
     * Process popup navigation events
     */
    void on_popup_navigation(const std::string& url);

    /**
     * Handle popup disconnection
     */
    void on_popup_disconnected();

    // ========================================================================
    // Feed Discovery (HTTP + XML parsing)
    // ========================================================================

    /**
     * Perform an HTTP GET with bearer token authorization
     * @return Response body, or empty string on failure
     */
    static std::string http_get(const std::string& url, const std::string& bearer_token);

    /**
     * Discover tenant feeds from the WVD feed discovery endpoint
     */
    std::vector<TenantFeed> discover_tenants(const std::string& bearer_token);

    /**
     * Fetch resources for a single tenant
     */
    std::vector<FeedResource> fetch_tenant_resources(const std::string& feed_url,
                                                      const std::string& bearer_token);

    /**
     * Download an RDP file and return its content
     */
    std::string fetch_rdp_content(const std::string& rdp_url,
                                   const std::string& bearer_token);

    /**
     * Import a single RDP resource into the database
     */
    bool import_resource(const FeedResource& resource,
                         const std::string& workspace_name,
                         const std::string& rdp_content,
                         const std::string& account_id = "");

    /**
     * Send a progress notification to the main window
     */
    void notify_progress(const std::string& message, int current, int total);

    // ========================================================================
    // State
    // ========================================================================

    ConfigManager& m_config_manager;
    webui::window* m_main_window = nullptr;

    // Popup window for authentication
    std::unique_ptr<webui::window> m_popup;
    std::mutex m_popup_mutex;

    // Synchronization for token extraction
    std::mutex m_token_mutex;
    std::condition_variable m_token_cv;
    std::string m_bearer_token;
    std::string m_auth_code;            // OAuth authorization code from redirect
    std::string m_code_verifier;        // PKCE code verifier for token exchange
    std::string m_id_token;             // OIDC id_token (JWT with user claims)
    std::atomic<bool> m_token_found{false};
    std::atomic<bool> m_popup_closed{false};

    // True while navigating (ignore DISCONNECTED events)
    std::atomic<bool> m_navigating{false};

    // True while a discovery is in progress
    std::atomic<bool> m_busy{false};

    // Authentication timeout
    static constexpr auto AUTH_TIMEOUT = std::chrono::minutes(5);

    // WVD endpoints
    static constexpr std::string_view WVD_WEBCLIENT_URL =
        "https://client.wvd.microsoft.com/arm/webclient/index.html";
    static constexpr std::string_view WVD_FEED_DISCOVERY_URL =
        "https://client.wvd.microsoft.com/api/arm/feeddiscovery";

    // WVD OAuth constants
    static constexpr std::string_view WVD_CLIENT_ID =
        "a85cf173-4192-42f8-81fa-777a763e6e2c";
    static constexpr std::string_view WVD_REDIRECT_URI =
        "https://client.wvd.microsoft.com/arm/webclient/index.html";
    static constexpr std::string_view WVD_SCOPE =
        "https://www.wvd.microsoft.com/.default openid profile offline_access";
    static constexpr std::string_view MS_TOKEN_ENDPOINT =
        "https://login.microsoftonline.com/common/oauth2/v2.0/token";
    static constexpr std::string_view MS_AUTHORIZE_ENDPOINT =
        "https://login.microsoftonline.com/common/oauth2/v2.0/authorize";

    // Global instance pointer for static callbacks
    static FeedDiscoveryManager* s_instance;
};


