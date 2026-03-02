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

#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <random>
#include <cstring>
#include <vector>

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

// HTML-decode common entities (&lt;, &gt;, &amp;, &quot;, &apos;)
std::string html_decode(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            if (s.compare(i, 4, "&lt;") == 0) { result += '<'; i += 3; }
            else if (s.compare(i, 4, "&gt;") == 0) { result += '>'; i += 3; }
            else if (s.compare(i, 5, "&amp;") == 0) { result += '&'; i += 4; }
            else if (s.compare(i, 6, "&quot;") == 0) { result += '"'; i += 5; }
            else if (s.compare(i, 6, "&apos;") == 0) { result += '\''; i += 5; }
            else { result += s[i]; }
        } else {
            result += s[i];
        }
    }
    return result;
}

} // anonymous namespace

// ============================================================================
// URL-decode helper (for extracting token from callback URL)
// ============================================================================

namespace {

std::string url_decode(const std::string& src) {
    std::string result;
    result.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()) {
            unsigned int val = 0;
            std::istringstream iss(src.substr(i + 1, 2));
            if (iss >> std::hex >> val) {
                result += static_cast<char>(val);
                i += 2;
            } else {
                result += src[i];
            }
        } else if (src[i] == '+') {
            result += ' ';
        } else {
            result += src[i];
        }
    }
    return result;
}

// Base64url decode (JWT payload decoding)
std::string base64url_decode(const std::string& input) {
    // Convert base64url to standard base64
    std::string b64 = input;
    for (auto& c : b64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    // Add padding
    while (b64.size() % 4 != 0) {
        b64 += '=';
    }

    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    decoded.reserve(b64.size() * 3 / 4);
    uint32_t bits = 0;
    int bit_count = 0;
    for (char c : b64) {
        if (c == '=') break;
        auto pos = chars.find(c);
        if (pos == std::string::npos) continue;
        bits = (bits << 6) | static_cast<uint32_t>(pos);
        bit_count += 6;
        if (bit_count >= 8) {
            bit_count -= 8;
            decoded += static_cast<char>((bits >> bit_count) & 0xFF);
        }
    }
    return decoded;
}

// Extract a string field from a JWT payload
std::string jwt_extract_field(const std::string& jwt, const std::string& field) {
    // Split JWT by '.'
    auto dot1 = jwt.find('.');
    if (dot1 == std::string::npos) return "";
    auto dot2 = jwt.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return "";

    std::string payload = base64url_decode(jwt.substr(dot1 + 1, dot2 - dot1 - 1));
    if (payload.empty()) return "";

    // Search for "field":"value"
    std::string needle = "\"" + field + "\"";
    auto pos = payload.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();

    // Skip whitespace and colon
    while (pos < payload.size() && (payload[pos] == ' ' || payload[pos] == ':')) pos++;
    if (pos >= payload.size() || payload[pos] != '"') return "";
    pos++; // skip opening quote

    std::string value;
    while (pos < payload.size() && payload[pos] != '"') {
        if (payload[pos] == '\\' && pos + 1 < payload.size()) {
            pos++;
        }
        value += payload[pos];
        pos++;
    }
    return value;
}

// Generate a simple UUID v4
std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 15);
    const char* hex = "0123456789abcdef";
    std::string uuid(36, '-');
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        else if (i == 14) uuid[i] = '4';
        else if (i == 19) uuid[i] = hex[(dist(gen) & 0x3) | 0x8];
        else uuid[i] = hex[dist(gen)];
    }
    return uuid;
}

// ============================================================================
// PKCE helpers: SHA-256 + base64url + code verifier generation
// ============================================================================

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
    memcpy(msg.data(), data, len);
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

// Base64url encode (no padding, URL-safe alphabet)
std::string base64url_encode(const uint8_t* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    result.reserve((len * 4 + 2) / 3);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < len) n |= uint32_t(data[i+1]) << 8;
        if (i + 2 < len) n |= uint32_t(data[i+2]);
        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        if (i + 1 < len) result += table[(n >> 6) & 0x3F];
        if (i + 2 < len) result += table[n & 0x3F];
    }
    return result;
}

// URL-encode a string
std::string url_encode(const std::string& s) {
    std::string result;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += static_cast<char>(c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            result += buf;
        }
    }
    return result;
}

// Generate PKCE code verifier (128 URL-safe random chars)
std::string generate_code_verifier() {
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    std::string verifier;
    verifier.reserve(128);
    for (int i = 0; i < 128; i++) {
        verifier += charset[dist(rng)];
    }
    return verifier;
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
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[FeedDiscovery] curl_easy_init failed" << std::endl;
        return "";
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + bearer_token;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Accept: application/x-msts-radc-discovery+xml,text/xml");
    headers = curl_slist_append(headers, "Origin: https://client.wvd.microsoft.com");
    headers = curl_slist_append(headers, "Referer: https://client.wvd.microsoft.com/arm/webclient/index.html");
    headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 Edg/131.0.0.0");
    // RD webclient identification — required by the WVD feed discovery API
    headers = curl_slist_append(headers, "x-ms-user-agent: com.microsoft.rdc.html/2.0.69.1 rdhtml-sdk/2.0.4");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, feed_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "[FeedDiscovery] curl GET failed for " << url
                  << ": " << curl_easy_strerror(res) << std::endl;
        response.clear();
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200) {
            std::cerr << "[FeedDiscovery] HTTP " << http_code << " for " << url << std::endl;
            if (!response.empty()) {
                std::cerr << "[FeedDiscovery] Response body: "
                          << response.substr(0, 500) << std::endl;
            }
            response.clear();
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

// ============================================================================
// XML Parsing: tenant feed discovery
// ============================================================================

std::vector<TenantFeed> FeedDiscoveryManager::discover_tenants(
    const std::string& bearer_token) {

    std::vector<TenantFeed> tenants;
    std::cout << "[FeedDiscovery] Fetching tenant feeds from " << WVD_FEED_DISCOVERY_URL << std::endl;

    std::string xml = http_get(WVD_FEED_DISCOVERY_URL, bearer_token);
    if (xml.empty()) {
        std::cerr << "[FeedDiscovery] Empty response from feed discovery endpoint" << std::endl;
        return tenants;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(xml.c_str());
    if (!parse_result) {
        std::cerr << "[FeedDiscovery] XML parse error: " << parse_result.description() << std::endl;
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
        std::cerr << "[FeedDiscovery] No TenantFeedURLs element found in response" << std::endl;
        return tenants;
    }

    for (auto& node : root.children()) {
        std::string name = node.name();
        if (name.find("TenantFeedURL") == std::string::npos) {
            continue;
        }

        TenantFeed tenant;
        tenant.tenant_id = node.attribute("TenantId").as_string();
        tenant.tenant_display_name = html_decode(node.attribute("TenantDisplayName").as_string());
        tenant.workspace_name = html_decode(node.attribute("WorkspaceName").as_string());
        tenant.feed_url = html_decode(node.attribute("FeedURL").as_string());
        tenant.geo = node.attribute("Geo").as_string();
        tenant.arm_path = node.attribute("ArmPath").as_string();
        tenant.hub_discovery_url = html_decode(node.attribute("HubDiscoveryURL").as_string());

        if (!tenant.tenant_id.empty() && !tenant.feed_url.empty()) {
            std::cout << "[FeedDiscovery] Found tenant: " << tenant.workspace_name
                      << " (ID: " << tenant.tenant_id << ")" << std::endl;
            tenants.push_back(std::move(tenant));
        }
    }

    std::cout << "[FeedDiscovery] Discovered " << tenants.size() << " tenants" << std::endl;
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
        std::cerr << "[FeedDiscovery] Empty response from feed URL: " << feed_url << std::endl;
        return resources;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(xml.c_str());
    if (!parse_result) {
        std::cerr << "[FeedDiscovery] XML parse error: " << parse_result.description() << std::endl;
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
        std::cerr << "[FeedDiscovery] No ResourceCollection element found" << std::endl;
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
            resource.title = html_decode(res_node.attribute("Title").as_string());
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
                        resource.rdp_url = html_decode(file_node.attribute("URL").as_string());
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
                    resource.icon_url = html_decode(icon32.attribute("FileURL").as_string());
                }
            }

            if (!resource.id.empty() && !resource.rdp_url.empty()) {
                std::cout << "[FeedDiscovery]   Resource: " << resource.title
                          << " (" << resource.type << ")" << std::endl;
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
                                            const std::string& rdp_content) {
    if (rdp_content.empty()) {
        return false;
    }

    auto parsed = RDPFileParser::parse_content(rdp_content);
    if (!parsed.has_value()) {
        std::cerr << "[FeedDiscovery] Failed to parse RDP content for "
                  << resource.title << std::endl;
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

    // Display settings
    profile.dynamic_resolution = rdp.dynamic_resolution;
    profile.clipboard = rdp.redirect_clipboard;

    // If the connection name already exists, make it unique by appending workspace
    auto existing = m_config_manager.get_connection(profile.name);
    if (existing.has_value() && existing->folder != profile.folder) {
        profile.name = workspace_name + " - " + resource.title;
    }

    // Ensure the folder exists
    m_config_manager.create_folder(profile.folder);

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

    std::string js = "if (typeof onFeedDiscoveryProgress === 'function') "
                     "onFeedDiscoveryProgress({\"message\":\"" +
                     json_utils::escape_string(message) + "\","
                     "\"current\":" + std::to_string(current) + ","
                     "\"total\":" + std::to_string(total) + "});";
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
    std::cout << "[FeedDiscovery] Navigation: " << url << std::endl;

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
            code = url_decode(code);
        }

        if (!code.empty()) {
            std::cout << "[FeedDiscovery] Auth code received (length=" << code.size() << ")" << std::endl;
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
    std::cout << "[FeedDiscovery] Popup disconnected" << std::endl;

    if (m_navigating) {
        std::cout << "[FeedDiscovery] Expected disconnect during navigation, ignoring" << std::endl;
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
    m_code_verifier = generate_code_verifier();
    uint8_t hash[32];
    sha256(reinterpret_cast<const uint8_t*>(m_code_verifier.data()),
           m_code_verifier.size(), hash);
    std::string code_challenge = base64url_encode(hash, 32);

    // Build the OAuth2 authorization URL with PKCE
    std::string auth_url = std::string(MS_AUTHORIZE_ENDPOINT)
        + "?client_id=" + WVD_CLIENT_ID
        + "&scope=" + url_encode(WVD_SCOPE)
        + "&redirect_uri=" + url_encode(WVD_REDIRECT_URI)
        + "&response_type=code"
        + "&response_mode=fragment"
        + "&code_challenge=" + code_challenge
        + "&code_challenge_method=S256";

    std::cout << "[FeedDiscovery] PKCE code_challenge: " << code_challenge << std::endl;
    std::cout << "[FeedDiscovery] Auth URL: " << auth_url << std::endl;

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
        std::cerr << "[FeedDiscovery] Failed to show popup window" << std::endl;
        return "";
    }

    // Wait briefly for the popup to initialize, then navigate to auth URL
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    {
        std::lock_guard<std::mutex> popup_lock(m_popup_mutex);
        if (m_popup) {
            m_navigating = true;
            std::cout << "[FeedDiscovery] Navigating to Microsoft login" << std::endl;
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
            std::cout << "[FeedDiscovery] Closing popup window" << std::endl;
            m_popup->close();
            // Intentionally leak to avoid GTK use-after-free (same as AADAuthHandler)
            m_popup.release();
        }
    }

    if (auth_code.empty()) {
        std::cout << "[FeedDiscovery] Authentication failed or timed out" << std::endl;
        return "";
    }

    // Exchange the authorization code for an access token using PKCE
    std::cout << "[FeedDiscovery] Exchanging auth code for access token..." << std::endl;
    std::string token = exchange_code_for_token(auth_code);

    if (token.empty()) {
        std::cout << "[FeedDiscovery] Token exchange failed" << std::endl;
    } else {
        std::cout << "[FeedDiscovery] Authentication successful (token length="
                  << token.size() << ")" << std::endl;
    }

    return token;
}

// ============================================================================
// PKCE token exchange
// ============================================================================

std::string FeedDiscoveryManager::exchange_code_for_token(const std::string& code) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[FeedDiscovery] curl_easy_init failed" << std::endl;
        return "";
    }

    // Build POST body for the token exchange
    std::string post_data =
        "client_id=" + std::string(WVD_CLIENT_ID)
        + "&code=" + url_encode(code)
        + "&redirect_uri=" + url_encode(WVD_REDIRECT_URI)
        + "&grant_type=authorization_code"
        + "&code_verifier=" + url_encode(m_code_verifier)
        + "&scope=" + url_encode(WVD_SCOPE);

    std::cout << "[FeedDiscovery] Token request to " << MS_TOKEN_ENDPOINT << std::endl;

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    // The WVD client app is registered as a SPA in Azure AD.
    // SPA token exchanges require a cross-origin request with a matching Origin header.
    headers = curl_slist_append(headers, "Origin: https://client.wvd.microsoft.com");

    curl_easy_setopt(curl, CURLOPT_URL, MS_TOKEN_ENDPOINT);
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
        std::cerr << "[FeedDiscovery] Token exchange curl error: "
                  << curl_easy_strerror(res) << std::endl;
        return "";
    }

    // Parse JSON response for access_token
    json_error_t error;
    json_t* root = json_loads(response.c_str(), 0, &error);
    if (!root) {
        std::cerr << "[FeedDiscovery] Token response JSON parse error: "
                  << error.text << std::endl;
        std::cerr << "[FeedDiscovery] Response body: " << response << std::endl;
        return "";
    }

    std::string token = json_utils::get_string(root, "access_token");
    if (token.empty()) {
        std::string err_desc = json_utils::get_string(root, "error_description");
        std::string err_code = json_utils::get_string(root, "error");
        std::cerr << "[FeedDiscovery] Token exchange failed: " << err_code
                  << " - " << err_desc << std::endl;
    }

    // Also extract the id_token (OIDC JWT with user claims like name, email)
    // The access_token for WVD is opaque/encrypted and can't be parsed for user info
    m_id_token = json_utils::get_string(root, "id_token");
    if (!m_id_token.empty()) {
        std::cout << "[FeedDiscovery] id_token received (length=" << m_id_token.size() << ")" << std::endl;
    }

    json_decref(root);
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
    std::string user_display_name = jwt_extract_field(id_src, "name");
    std::string user_upn = jwt_extract_field(id_src, "upn");
    std::string user_email = jwt_extract_field(id_src, "preferred_username");
    if (user_email.empty()) user_email = user_upn;
    if (user_display_name.empty()) user_display_name = user_email;
    if (user_display_name.empty()) user_display_name = "WVD Account";

    std::cout << "[FeedDiscovery] Authenticated as: " << user_display_name
              << " (" << user_email << ")" << std::endl;

    // Step 3: Create or update the feed account
    auto now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    FeedAccount account;
    if (account_id.empty()) {
        // New account — generate UUID
        account.id = generate_uuid();
        account.display_name = user_display_name;
        account.email = user_email;
        account.last_synced = now;
        m_config_manager.add_feed_account(account);
        std::cout << "[FeedDiscovery] Created new account: " << account.id << std::endl;
    } else {
        // Existing account — update
        account.id = account_id;
        account.display_name = user_display_name;
        account.email = user_email;
        account.last_synced = now;
        m_config_manager.update_feed_account(account);
        std::cout << "[FeedDiscovery] Updated account: " << account.id << std::endl;
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

        std::cout << "[FeedDiscovery] Fetching resources for tenant: "
                  << tenant.workspace_name << std::endl;

        auto resources = fetch_tenant_resources(tenant.feed_url, token);
        total_resources += static_cast<int>(resources.size());

        for (const auto& resource : resources) {
            std::cout << "[FeedDiscovery] Downloading RDP for: " << resource.title << std::endl;

            std::string rdp_content = fetch_rdp_content(resource.rdp_url, token);
            if (rdp_content.empty()) {
                std::cerr << "[FeedDiscovery] Failed to download RDP for "
                          << resource.title << std::endl;
                continue;
            }

            if (import_resource(resource, tenant.workspace_name, rdp_content)) {
                imported++;
                std::cout << "[FeedDiscovery] Imported: " << resource.title
                          << " into Feeds/" << tenant.workspace_name << std::endl;
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

    std::cout << "[FeedDiscovery] Discovery complete: " << imported
              << " resources imported from " << result.tenant_count << " tenants" << std::endl;

    m_busy = false;
    return result;
}
