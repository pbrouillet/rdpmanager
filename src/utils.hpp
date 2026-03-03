/**
 * Shared utility functions
 *
 * Consolidated URL encoding/decoding, base64url, UUID generation,
 * and other helpers previously duplicated across multiple source files.
 */

#pragma once

#include <string>
#include <string_view>
#include <format>
#include <cstdint>
#include <cstddef>
#include <sstream>
#include <random>
#include <cctype>
#include <cstring>
#include <vector>
#include <optional>

namespace utils {

// ============================================================================
// URL encode / decode
// ============================================================================

[[nodiscard]] inline std::string url_decode(std::string_view src) {
    std::string result;
    result.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()) {
            unsigned int val = 0;
            std::istringstream iss(std::string{src.substr(i + 1, 2)});
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

[[nodiscard]] inline std::string url_encode(std::string_view s) {
    std::string result;
    for (auto c : s) {
        auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || uc == '-' || uc == '_' || uc == '.' || uc == '~') {
            result += static_cast<char>(uc);
        } else {
            result += std::format("%{:02X}", uc);
        }
    }
    return result;
}

// ============================================================================
// Base64url encode / decode
// ============================================================================

[[nodiscard]] inline std::string base64url_decode(std::string_view input) {
    // Convert base64url to standard base64
    std::string b64{input};
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

[[nodiscard]] inline std::string base64url_encode(const uint8_t* data, size_t len) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    result.reserve((len * 4 + 2) / 3);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < len) n |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < len) n |= uint32_t(data[i + 2]);
        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        if (i + 1 < len) result += table[(n >> 6) & 0x3F];
        if (i + 2 < len) result += table[n & 0x3F];
    }
    return result;
}

// ============================================================================
// UUID v4 generation
// ============================================================================

[[nodiscard]] inline std::string generate_uuid() {
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
// JWT helpers
// ============================================================================

/**
 * Extract a string field from a JWT payload (base64url-encoded middle segment).
 */
[[nodiscard]] inline std::string jwt_extract_field(std::string_view jwt, std::string_view field) {
    auto dot1 = jwt.find('.');
    if (dot1 == std::string_view::npos) return "";
    auto dot2 = jwt.find('.', dot1 + 1);
    if (dot2 == std::string_view::npos) return "";

    auto payload = base64url_decode(jwt.substr(dot1 + 1, dot2 - dot1 - 1));
    if (payload.empty()) return "";

    auto needle = std::format("\"{}\"", field);
    auto pos = payload.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();

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

/**
 * Extract the "exp" (expiration) claim from a JWT as epoch seconds.
 */
[[nodiscard]] inline std::optional<int64_t> jwt_extract_expiration(std::string_view token) {
    const auto first_dot = token.find('.');
    if (first_dot == std::string_view::npos) return std::nullopt;
    const auto second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) return std::nullopt;

    const auto payload = base64url_decode(
        token.substr(first_dot + 1, second_dot - first_dot - 1));
    if (payload.empty()) return std::nullopt;

    const std::string needle = "\"exp\":";
    size_t exp_pos = payload.find(needle);
    if (exp_pos == std::string::npos) return std::nullopt;
    exp_pos += needle.size();
    while (exp_pos < payload.size() &&
           std::isspace(static_cast<unsigned char>(payload[exp_pos]))) {
        exp_pos++;
    }

    size_t end_pos = exp_pos;
    while (end_pos < payload.size() &&
           std::isdigit(static_cast<unsigned char>(payload[end_pos]))) {
        end_pos++;
    }
    if (end_pos == exp_pos) return std::nullopt;

    try {
        return std::stoll(payload.substr(exp_pos, end_pos - exp_pos));
    } catch (...) {
        return std::nullopt;
    }
}

// ============================================================================
// PKCE helpers
// ============================================================================

/**
 * Generate a PKCE code verifier (128 URL-safe random chars).
 */
[[nodiscard]] inline std::string generate_code_verifier() {
    static const char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
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

// ============================================================================
// HTML helpers
// ============================================================================

/**
 * Decode common HTML entities (&lt; &gt; &amp; &quot; &apos;).
 */
[[nodiscard]] inline std::string html_decode(std::string_view s) {
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

// ============================================================================
// Popup HTML placeholder (shared by AADAuthHandler & FeedDiscoveryManager)
// ============================================================================

inline const char* auth_popup_placeholder_html() {
    return
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<title>Azure AD Authentication</title>"
        "<style>"
        "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; "
        "       display: flex; flex-direction: column; align-items: center; justify-content: center; "
        "       height: 100vh; margin: 0; background: #f5f5f5; }"
        "h1 { color: #333; }"
        ".spinner { width: 50px; height: 50px; border: 5px solid #e0e0e0; "
        "           border-top: 5px solid #0078d4; border-radius: 50%; animation: spin 1s linear infinite; }"
        "@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }"
        "</style>"
        "</head>"
        "<body>"
        "<h1>Azure AD Authentication</h1>"
        "<div class=\"spinner\"></div>"
        "<p id=\"status\">Setting up authentication...</p>"
        "</body>"
        "</html>";
}

} // namespace utils


