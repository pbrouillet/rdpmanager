/**
 * Feed Discovery Manager - Implementation
 *
 * Authenticates via WVD webclient popup, extracts bearer tokens from
 * sessionStorage, and uses libcurl + pugixml to discover and import
 * WVD/AVD feed resources as RDP connection profiles.
 */

#include "feed_discovery.hpp"
#include "config_manager.hpp"
#include "rdp_file_parser.hpp"
#include "json_utils.hpp"
#include "logger.hpp"
#include "utils.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <random>
#include <cstring>
#include <vector>
#include <format>

#include <curl/curl.h>
#include <pugixml.hpp>
#include <jansson.h>

// ============================================================================
// Static instance pointer
// ============================================================================

FeedDiscoveryManager* FeedDiscoveryManager::s_instance = nullptr;

// ============================================================================
// libcurl write callback
// ============================================================================

namespace {

size_t feed_curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<const char*>(contents), total);
    return total;
}

// SHA-256 implementation for PKCE code challenge generation.
// TODO: Replace with WinPR winpr_Digest(WINPR_MD_SHA256, ...) or OpenSSL
// EVP_Digest() once the build wiring is simplified — both are already linked.
void sha256(const uint8_t* data, size_t len, uint8_t hash[32]) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    auto rotr = [](uint32_t x, int n) -> uint32_t { return (x >> n) | (x << (32 - n)); };
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    std::vector<uint8_t> msg(padded_len, 0);
    std::memcpy(msg.data(), data, len);
    msg[len] = 0x80;
    uint64_t bit_len = len * 8;
    for (int i = 0; i < 8; i++)
        msg[padded_len - 1 - i] = static_cast<uint8_t>(bit_len >> (i * 8));
    for (size_t offset = 0; offset < padded_len; offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t(msg[offset+i*4]) << 24) | (uint32_t(msg[offset+i*4+1]) << 16) |
                   (uint32_t(msg[offset+i*4+2]) << 8) | uint32_t(msg[offset+i*4+3]);
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (h[i] >> 24) & 0xFF;
        hash[i*4+1] = (h[i] >> 16) & 0xFF;
        hash[i*4+2] = (h[i] >> 8) & 0xFF;
        hash[i*4+3] = h[i] & 0xFF;
    }
}

} // anonymous namespace

// ============================================================================
// Constructor / Destructor
// ============================================================================

FeedDiscoveryManager::FeedDiscoveryManager(ConfigManager& config_manager)
    : m_config_manager(config_manager)
{
    s_instance = this;
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

FeedDiscoveryManager::~FeedDiscoveryManager() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
    curl_global_cleanup();
}

void FeedDiscoveryManager::set_main_window(webui::window* window) {
    m_main_window = window;
}

// ============================================================================
// HTTP GET with bearer token
// ============================================================================

std::string FeedDiscoveryManager::http_get(const std::string& url,
                                            const std::string& bearer_token) {
    // RAII wrappers for curl handles
    struct CurlDeleter { void operator()(CURL* p) const noexcept { curl_easy_cleanup(p); } };
    struct SlistDeleter { void operator()(curl_slist* p) const noexcept { curl_slist_free_all(p); } };

    std::unique_ptr<CURL, CurlDeleter> curl{curl_easy_init()};
    if (!curl) {
        LOG_ERROR("FeedDiscovery", "curl_easy_init failed");
        return "";
    }

    std::string response;
    std::string auth_header = "Authorization: Bearer " + bearer_token;
    std::unique_ptr<curl_slist, SlistDeleter> headers{nullptr};
    auto append_header = [&](const char* h) {
        headers.reset(curl_slist_append(headers.release(), h));
    };
    append_header(auth_header.c_str());
    append_header("Accept: application/x-msts-radc-discovery+xml,text/xml");
    append_header("Origin: https://client.wvd.microsoft.com");
    append_header("Referer: https://client.wvd.microsoft.com/arm/webclient/index.html");
    append_header("User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 Edg/131.0.0.0");
    // RD webclient identification — required by the WVD feed discovery API
    append_header("x-ms-user-agent: com.microsoft.rdc.html/2.0.69.1 rdhtml-sdk/2.0.4");

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, feed_curl_write_cb);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);

    auto res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        LOG_ERROR("FeedDiscovery", "curl GET failed for " << url
                  << ": " << curl_easy_strerror(res));
        response.clear();
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200) {
            LOG_ERROR("FeedDiscovery", "HTTP " << http_code << " for " << url);
            if (!response.empty()) {
                LOG_DEBUG("FeedDiscovery", "Response body: "
                          << response.substr(0, 500));
            }
            response.clear();
        }
    }

    return response;
}

// ============================================================================
// XML Parsing: tenant feed discovery
// ============================================================================

std::vector<TenantFeed> FeedDiscoveryManager::discover_tenants(
    const std::string& bearer_token) {

    std::vector<TenantFeed> tenants;
    LOG_INFO("FeedDiscovery", "Fetching tenant feeds from " << WVD_FEED_DISCOVERY_URL);

    std::string xml = http_get(std::string{WVD_FEED_DISCOVERY_URL}, bearer_token);
    if (xml.empty()) {
        LOG_ERROR("FeedDiscovery", "Empty response from feed discovery endpoint");
        return tenants;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(xml.c_str());
    if (!parse_result) {
        LOG_ERROR("FeedDiscovery", "XML parse error: " << parse_result.description());
        return tenants;
    }

    // Navigate to TenantFeedURLs root element
    // The namespace is http://schemas.microsoft.com/ts/2014/03/tswfdiscovery
    auto root = doc.child("TenantFeedURLs");
    if (!root) {
        // Try with namespace prefix
        for (auto& child : doc.children()) {
            std::string name = child.name();
            if (name.find("TenantFeedURLs") != std::string::npos) {
                root = child;
                break;
            }
        }
    }

    if (!root) {
        LOG_ERROR("FeedDiscovery", "No TenantFeedURLs element found in response");
        return tenants;
    }

    for (auto& node : root.children()) {
        std::string name = node.name();
        if (name.find("TenantFeedURL") == std::string::npos) {
            continue;
        }

        TenantFeed tenant;
        tenant.tenant_id = node.attribute("TenantId").as_string();
        tenant.tenant_display_name = utils::html_decode(node.attribute("TenantDisplayName").as_string());
        tenant.workspace_name = utils::html_decode(node.attribute("WorkspaceName").as_string());
        tenant.feed_url = utils::html_decode(node.attribute("FeedURL").as_string());
        tenant.geo = node.attribute("Geo").as_string();
        tenant.arm_path = node.attribute("ArmPath").as_string();
        tenant.hub_discovery_url = utils::html_decode(node.attribute("HubDiscoveryURL").as_string());

        if (!tenant.tenant_id.empty() && !tenant.feed_url.empty()) {
            LOG_INFO("FeedDiscovery", "Found tenant: " << tenant.workspace_name
                      << " (ID: " << tenant.tenant_id << ")");
            tenants.push_back(std::move(tenant));
        }
    }

    LOG_INFO("FeedDiscovery", "Discovered " << tenants.size() << " tenants");
    return tenants;
}

// ============================================================================
// XML Parsing: tenant resource collection
// ============================================================================

std::vector<FeedResource> FeedDiscoveryManager::fetch_tenant_resources(
    const std::string& feed_url, const std::string& bearer_token) {

    std::vector<FeedResource> resources;

    std::string xml = http_get(feed_url, bearer_token);
    if (xml.empty()) {
        LOG_ERROR("FeedDiscovery", "Empty response from feed URL: " << feed_url);
        return resources;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(xml.c_str());
    if (!parse_result) {
        LOG_ERROR("FeedDiscovery", "XML parse error: " << parse_result.description());
        return resources;
    }

    // Find ResourceCollection > Publisher > Resources > Resource
    auto collection = doc.child("ResourceCollection");
    if (!collection) {
        for (auto& child : doc.children()) {
            std::string name = child.name();
            if (name.find("ResourceCollection") != std::string::npos) {
                collection = child;
                break;
            }
        }
    }

    if (!collection) {
        LOG_ERROR("FeedDiscovery", "No ResourceCollection element found");
        return resources;
    }

    for (auto& publisher : collection.children()) {
        std::string pub_name_str = publisher.name();
        if (pub_name_str.find("Publisher") == std::string::npos) {
            continue;
        }

        std::string publisher_name = publisher.attribute("Name").as_string();

        // Find Resources element
        auto resources_node = publisher.child("Resources");
        if (!resources_node) {
            for (auto& child : publisher.children()) {
                std::string name = child.name();
                if (name.find("Resources") != std::string::npos) {
                    resources_node = child;
                    break;
                }
            }
        }

        if (!resources_node) {
            continue;
        }

        for (auto& res_node : resources_node.children()) {
            std::string node_name = res_node.name();
            if (node_name.find("Resource") == std::string::npos) {
                continue;
            }

            FeedResource resource;
            resource.id = res_node.attribute("ID").as_string();
            resource.title = utils::html_decode(res_node.attribute("Title").as_string());
            resource.type = res_node.attribute("Type").as_string();
            resource.arm_path = res_node.attribute("ArmPath").as_string();
            resource.publisher_name = publisher_name;

            // Extract RDP file URL from HostingTerminalServers
            auto hosting = res_node.child("HostingTerminalServers");
            if (!hosting) {
                for (auto& child : res_node.children()) {
                    if (std::string(child.name()).find("HostingTerminalServers") != std::string::npos) {
                        hosting = child;
                        break;
                    }
                }
            }
            if (hosting) {
                auto server = hosting.child("HostingTerminalServer");
                if (!server) {
                    for (auto& child : hosting.children()) {
                        if (std::string(child.name()).find("HostingTerminalServer") != std::string::npos) {
                            server = child;
                            break;
                        }
                    }
                }
                if (server) {
                    auto file_node = server.child("ResourceFile");
                    if (!file_node) {
                        for (auto& child : server.children()) {
                            if (std::string(child.name()).find("ResourceFile") != std::string::npos) {
                                file_node = child;
                                break;
                            }
                        }
                    }
                    if (file_node) {
                        resource.rdp_url = utils::html_decode(file_node.attribute("URL").as_string());
                    }
                }
            }

            // Extract icon URL
            auto icons = res_node.child("Icons");
            if (!icons) {
                for (auto& child : res_node.children()) {
                    if (std::string(child.name()).find("Icons") != std::string::npos) {
                        icons = child;
                        break;
                    }
                }
            }
            if (icons) {
                auto icon32 = icons.child("Icon32");
                if (!icon32) {
                    for (auto& child : icons.children()) {
                        if (std::string(child.name()).find("Icon32") != std::string::npos) {
                            icon32 = child;
                            break;
                        }
                    }
                }
                if (icon32) {
                    resource.icon_url = utils::html_decode(icon32.attribute("FileURL").as_string());
                }
            }

            if (!resource.id.empty() && !resource.rdp_url.empty()) {
                LOG_DEBUG("FeedDiscovery", "  Resource: " << resource.title
                          << " (" << resource.type << ")");
                resources.push_back(std::move(resource));
            }
        }
    }

    return resources;
}

// ============================================================================
// Fetch RDP content
// ============================================================================

std::string FeedDiscoveryManager::fetch_rdp_content(const std::string& rdp_url,
                                                      const std::string& bearer_token) {
    return http_get(rdp_url, bearer_token);
}

// ============================================================================
// Import a single resource as a ConnectionProfile
// ============================================================================

bool FeedDiscoveryManager::import_resource(const FeedResource& resource,
                                            const std::string& workspace_name,
                                            const std::string& rdp_content,
                                            const std::string& account_id) {
    if (rdp_content.empty()) {
        return false;
    }

    auto parsed = RDPFileParser::parse_content(rdp_content);
    if (!parsed.has_value()) {
        LOG_ERROR("FeedDiscovery", "Failed to parse RDP content for "
                  << resource.title);
        return false;
    }

    const auto& rdp = parsed.value();

    // Build a ConnectionProfile from the parsed RDP data
    ConnectionProfile profile;
    profile.name = resource.title;
    profile.folder = "Feeds/" + workspace_name;
    profile.hostname = rdp.full_address;
    profile.port = (rdp.server_port > 0) ? rdp.server_port : 3389;
    profile.username = rdp.username;
    profile.domain = rdp.domain;

    // Gateway
    profile.gateway_hostname = rdp.gateway_hostname;

    // AAD settings
    profile.enable_rds_aad_auth = rdp.enable_rds_aad_auth;
    profile.target_is_aad_joined = rdp.target_is_aad_joined;
    profile.aad_tenant_id = rdp.aad_tenant_id;

    // AVD settings
    profile.load_balance_info = rdp.load_balance_info;
    profile.remote_desktop_name = rdp.remote_desktop_name;
    profile.wvd_endpoint_pool = rdp.wvd_endpoint_pool;
    profile.workspace_id = rdp.workspace_id;
    profile.arm_path = rdp.arm_path.empty() ? resource.arm_path : rdp.arm_path;
    profile.remote_application_program = rdp.remote_application_program;

    // Link this connection back to the feed account that imported it
    profile.source_account_id = account_id;

    // Display settings
    profile.dynamic_resolution = rdp.dynamic_resolution;
    profile.clipboard = rdp.redirect_clipboard;

    // If the connection name already exists, make it unique by appending workspace
    auto existing = m_config_manager.get_connection(profile.name);
    if (existing.has_value() && existing->folder != profile.folder) {
        profile.name = workspace_name + " - " + resource.title;
    }

    // Ensure the folder exists
    (void)m_config_manager.create_folder(profile.folder);

    // Save the connection
    return m_config_manager.save_connection(profile);
}

// ============================================================================
// Progress notification
// ============================================================================

void FeedDiscoveryManager::notify_progress(const std::string& message, int current, int total) {
    if (!m_main_window) {
        return;
    }

    auto js = std::format(
        R"(if (typeof onFeedDiscoveryProgress === 'function') onFeedDiscoveryProgress({{"message":"{}","current":{},"total":{}}}));)",
        json_utils::escape_string(message), current, total
    );
    m_main_window->run(js);
}

// ============================================================================
// Authentication popup
// ============================================================================

void FeedDiscoveryManager::s_handle_popup_events(webui::window::event* e) {
    if (!s_instance) return;

    if (e->get_type() == webui::NAVIGATION) {
        std::string url = e->get_string(0);
        s_instance->on_popup_navigation(url);
    } else if (e->get_type() == webui::DISCONNECTED) {
        s_instance->on_popup_disconnected();
    }
}

void FeedDiscoveryManager::on_popup_navigation(const std::string& url) {
    LOG_INFO("FeedDiscovery", "Navigation: " << url);

    // Always track navigation to avoid premature DISCONNECTED handling
    if (url.find("login.microsoftonline.com") != std::string::npos ||
        url.find("login.microsoft.com") != std::string::npos ||
        url.find("login.live.com") != std::string::npos ||
        url.find("client.wvd.microsoft.com") != std::string::npos ||
        url.find("microsoftazuread-sso.com") != std::string::npos ||
        url.find("fpt.dfp.microsoft.com") != std::string::npos) {
        m_navigating = true;
    }

    // Detect the redirect back to the webclient carrying the auth code.
    // Microsoft returns: .../index.html#code=<AUTH_CODE>&client_info=...&...
    if (url.find(WVD_REDIRECT_URI) != std::string::npos &&
        url.find("code=") != std::string::npos) {

        // Extract the code from URL fragment (#...) or query (?...)
        std::string params_str;
        auto hash_pos = url.find('#');
        if (hash_pos != std::string::npos) {
            params_str = url.substr(hash_pos + 1);
        } else {
            auto q_pos = url.find('?');
            if (q_pos != std::string::npos) {
                params_str = url.substr(q_pos + 1);
            }
        }

        // Find code= parameter
        std::string code;
        auto code_pos = params_str.find("code=");
        if (code_pos != std::string::npos) {
            auto value_start = code_pos + 5;
            auto amp_pos = params_str.find('&', value_start);
            code = params_str.substr(value_start,
                amp_pos != std::string::npos ? amp_pos - value_start : std::string::npos);
            code = utils::url_decode(code);
        }

        if (!code.empty()) {
            LOG_INFO("FeedDiscovery", "Auth code received (length=" << code.size() << ")");
            {
                std::lock_guard<std::mutex> lock(m_token_mutex);
                m_auth_code = code;
                m_token_found = true;
            }
            m_token_cv.notify_all();
            return;
        }
    }
}

void FeedDiscoveryManager::on_popup_disconnected() {
    LOG_INFO("FeedDiscovery", "Popup disconnected");

    if (m_navigating) {
        LOG_INFO("FeedDiscovery", "Expected disconnect during navigation, ignoring");
        return;
    }

    // User closed the popup before completing auth
    {
        std::lock_guard<std::mutex> lock(m_token_mutex);
        m_popup_closed = true;
    }
    m_token_cv.notify_all();
}

std::string FeedDiscoveryManager::authenticate_popup() {
    std::unique_lock<std::mutex> lock(m_token_mutex);

    // Reset state
    m_bearer_token.clear();
    m_auth_code.clear();
    m_token_found = false;
    m_popup_closed = false;
    m_navigating = false;

    lock.unlock();

    // Generate PKCE pair
    m_code_verifier = utils::generate_code_verifier();
    uint8_t hash[32];
    sha256(reinterpret_cast<const uint8_t*>(m_code_verifier.data()),
           m_code_verifier.size(), hash);
    std::string code_challenge = utils::base64url_encode(hash, 32);

    // Build the OAuth2 authorization URL with PKCE
    auto auth_url = std::format(
        "{}?client_id={}&scope={}&redirect_uri={}&response_type=code&response_mode=fragment&code_challenge={}&code_challenge_method=S256",
        MS_AUTHORIZE_ENDPOINT, WVD_CLIENT_ID,
        utils::url_encode(WVD_SCOPE), utils::url_encode(WVD_REDIRECT_URI),
        code_challenge
    );

    LOG_INFO("FeedDiscovery", "PKCE code_challenge: " << code_challenge);
    LOG_INFO("FeedDiscovery", "Auth URL: " << auth_url);

    // Notify main window
    if (m_main_window) {
        m_main_window->run("if (typeof showToast === 'function') "
                           "showToast('Opening WVD login window...', 'info', 5000);");
    }

    // Create the popup window
    {
        std::lock_guard<std::mutex> popup_lock(m_popup_mutex);
        m_popup = std::make_unique<webui::window>();
        m_popup->set_size(900, 750);
        m_popup->set_navigate_passthrough(true);

        // Bind event handler to intercept NAVIGATION and DISCONNECTED events
        m_popup->bind("", s_handle_popup_events);
    }

    // Show placeholder page
    std::string placeholder_html = R"HTML(
        <!DOCTYPE html>
        <html>
        <head>
            <title>WVD Feed Discovery - Login</title>
            <script src="webui.js"></script>
            <style>
                body {
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
                    display: flex; flex-direction: column; align-items: center;
                    justify-content: center; height: 100vh; margin: 0;
                    background: #1a1a2e; color: #e0e0e0;
                }
                h1 { color: #a0a0ff; margin-bottom: 20px; }
                .spinner {
                    width: 50px; height: 50px;
                    border: 5px solid #333; border-top: 5px solid #0078d4;
                    border-radius: 50%; animation: spin 1s linear infinite;
                }
                @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
                p { color: #888; margin-top: 20px; }
            </style>
        </head>
        <body>
            <h1>WVD Feed Discovery</h1>
            <div class="spinner"></div>
            <p>Redirecting to Microsoft login...</p>
        </body>
        </html>
    )HTML";

    bool shown = false;
    {
        std::lock_guard<std::mutex> popup_lock(m_popup_mutex);
        if (m_popup) {
            shown = m_popup->show(placeholder_html);
        }
    }

    if (!shown) {
        LOG_ERROR("FeedDiscovery", "Failed to show popup window");
        return "";
    }

    // Wait briefly for the popup to initialize, then navigate to auth URL
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    {
        std::lock_guard<std::mutex> popup_lock(m_popup_mutex);
        if (m_popup) {
            m_navigating = true;
            LOG_INFO("FeedDiscovery", "Navigating to Microsoft login");
            m_popup->navigate(auth_url);
        }
    }

    // Wait for auth code or timeout
    lock.lock();
    m_token_cv.wait_for(lock, AUTH_TIMEOUT, [this] {
        return m_token_found.load() || m_popup_closed.load();
    });

    std::string auth_code = m_auth_code;
    lock.unlock();

    // Close the popup
    {
        std::lock_guard<std::mutex> popup_lock(m_popup_mutex);
        if (m_popup) {
            LOG_INFO("FeedDiscovery", "Closing popup window");
            m_popup->close();
            // Intentionally leak to avoid GTK use-after-free (same as AADAuthHandler)
            m_popup.release();
        }
    }

    if (auth_code.empty()) {
        LOG_INFO("FeedDiscovery", "Authentication failed or timed out");
        return "";
    }

    // Exchange the authorization code for an access token using PKCE
    LOG_INFO("FeedDiscovery", "Exchanging auth code for access token...");
    std::string token = exchange_code_for_token(auth_code);

    if (token.empty()) {
        LOG_INFO("FeedDiscovery", "Token exchange failed");
    } else {
        LOG_INFO("FeedDiscovery", "Authentication successful (token length="
                  << token.size() << ")");
    }

    return token;
}

// ============================================================================
// PKCE token exchange
// ============================================================================

std::string FeedDiscoveryManager::exchange_code_for_token(const std::string& code) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("FeedDiscovery", "curl_easy_init failed");
        return "";
    }

    // Build POST body for the token exchange
    auto post_data = std::format(
        "client_id={}&code={}&redirect_uri={}&grant_type=authorization_code&code_verifier={}&scope={}",
        WVD_CLIENT_ID, utils::url_encode(code),
        utils::url_encode(WVD_REDIRECT_URI),
        utils::url_encode(m_code_verifier),
        utils::url_encode(WVD_SCOPE)
    );

    LOG_INFO("FeedDiscovery", "Token request to " << MS_TOKEN_ENDPOINT);

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    // The WVD client app is registered as a SPA in Azure AD.
    // SPA token exchanges require a cross-origin request with a matching Origin header.
    headers = curl_slist_append(headers, "Origin: https://client.wvd.microsoft.com");

    curl_easy_setopt(curl, CURLOPT_URL, std::string{MS_TOKEN_ENDPOINT}.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, feed_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("FeedDiscovery", "Token exchange curl error: "
                  << curl_easy_strerror(res));
        return "";
    }

    // Parse JSON response for access_token
    json_error_t error;
    json_utils::JsonPtr root{json_loads(response.c_str(), 0, &error)};
    if (!root) {
        LOG_ERROR("FeedDiscovery", "Token response JSON parse error: "
                  << error.text);
        LOG_ERROR("FeedDiscovery", "Response body: " << response);
        return "";
    }

    std::string token = json_utils::get_string(root.get(), "access_token");
    if (token.empty()) {
        auto err_desc = json_utils::get_string(root.get(), "error_description");
        auto err_code = json_utils::get_string(root.get(), "error");
        LOG_ERROR("FeedDiscovery", "Token exchange failed: " << err_code
                  << " - " << err_desc);
    }

    // Also extract the id_token (OIDC JWT with user claims like name, email)
    // The access_token for WVD is opaque/encrypted and can't be parsed for user info
    m_id_token = json_utils::get_string(root.get(), "id_token");
    if (!m_id_token.empty()) {
        LOG_INFO("FeedDiscovery", "id_token received (length=" << m_id_token.size() << ")");
    }

    return token;
}

// ============================================================================
// Main discovery workflow
// ============================================================================

FeedDiscoveryResult FeedDiscoveryManager::discover_and_import(
    const std::string& account_id) {

    FeedDiscoveryResult result;

    if (m_busy.exchange(true)) {
        result.error = "Feed discovery is already in progress";
        return result;
    }

    // Step 1: Authenticate
    notify_progress("Authenticating...", 0, 0);
    std::string token = authenticate_popup();

    if (token.empty()) {
        m_busy = false;
        result.error = "Authentication failed or was cancelled";
        return result;
    }

    // Step 2: Extract identity from the OIDC id_token (JWT with user claims)
    // The WVD access_token is opaque/encrypted; user info is only in the id_token
    std::string id_src = m_id_token.empty() ? token : m_id_token;
    std::string user_display_name = utils::jwt_extract_field(id_src, "name");
    std::string user_upn = utils::jwt_extract_field(id_src, "upn");
    std::string user_email = utils::jwt_extract_field(id_src, "preferred_username");
    if (user_email.empty()) user_email = user_upn;
    if (user_display_name.empty()) user_display_name = user_email;
    if (user_display_name.empty()) user_display_name = "WVD Account";

    LOG_INFO("FeedDiscovery", "Authenticated as: " << user_display_name
              << " (" << user_email << ")");

    // Step 3: Create or update the feed account
    auto now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    FeedAccount account;
    if (account_id.empty()) {
        // New account — generate UUID
        account.id = utils::generate_uuid();
        account.display_name = user_display_name;
        account.email = user_email;
        account.last_synced = now;
        (void)m_config_manager.add_feed_account(account);
        LOG_INFO("FeedDiscovery", "Created new account: " << account.id);
    } else {
        // Existing account — update
        account.id = account_id;
        account.display_name = user_display_name;
        account.email = user_email;
        account.last_synced = now;
        (void)m_config_manager.update_feed_account(account);
        LOG_INFO("FeedDiscovery", "Updated account: " << account.id);
    }

    result.account_id = account.id;
    result.account_display_name = account.display_name;

    // Step 4: Discover tenants
    notify_progress("Discovering tenant feeds...", 0, 0);
    auto tenants = discover_tenants(token);

    if (tenants.empty()) {
        m_busy = false;
        result.error = "No tenant feeds found";
        // Account was already created, keep it
        result.success = true;
        return result;
    }

    result.tenant_count = static_cast<int>(tenants.size());

    // Step 3: Iterate over each tenant, fetch resources, import RDP files
    int total_resources = 0;
    int imported = 0;
    int tenant_idx = 0;

    for (const auto& tenant : tenants) {
        tenant_idx++;
        notify_progress("Fetching resources from " + tenant.workspace_name + "...",
                        tenant_idx, result.tenant_count);

        LOG_INFO("FeedDiscovery", "Fetching resources for tenant: "
                  << tenant.workspace_name);

        auto resources = fetch_tenant_resources(tenant.feed_url, token);
        total_resources += static_cast<int>(resources.size());

        for (const auto& resource : resources) {
            LOG_INFO("FeedDiscovery", "Downloading RDP for: " << resource.title);

            std::string rdp_content = fetch_rdp_content(resource.rdp_url, token);
            if (rdp_content.empty()) {
                LOG_ERROR("FeedDiscovery", "Failed to download RDP for "
                          << resource.title);
                continue;
            }

            if (import_resource(resource, tenant.workspace_name, rdp_content, account.id)) {
                imported++;
                LOG_INFO("FeedDiscovery", "Imported: " << resource.title
                          << " into Feeds/" << tenant.workspace_name);
            }
        }
    }

    result.success = true;
    result.imported_count = imported;

    // Notify completion
    std::string completion_msg = "Imported " + std::to_string(imported) +
                                  " resources from " + std::to_string(result.tenant_count) +
                                  " workspaces";
    notify_progress(completion_msg, result.tenant_count, result.tenant_count);

    if (m_main_window) {
        std::string js = "if (typeof onFeedDiscoveryComplete === 'function') "
                         "onFeedDiscoveryComplete({\"success\":true,"
                         "\"imported_count\":" + std::to_string(imported) + ","
                         "\"tenant_count\":" + std::to_string(result.tenant_count) + ","
                         "\"account_id\":\"" + json_utils::escape_string(result.account_id) + "\","
                         "\"account_display_name\":\"" + json_utils::escape_string(result.account_display_name) + "\","
                         "\"message\":\"" + json_utils::escape_string(completion_msg) + "\"});";
        m_main_window->run(js);
    }

    LOG_INFO("FeedDiscovery", "Discovery complete: " << imported
              << " resources imported from " << result.tenant_count << " tenants");

    m_busy = false;
    return result;
}
