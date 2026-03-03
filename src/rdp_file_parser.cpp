/**
 * RDP File Parser Implementation
 * 
 * Parses Microsoft Remote Desktop Connection (.rdp/.rdpw) files.
 */

#include "rdp_file_parser.hpp"
#include "json_utils.hpp"
#include "logger.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cctype>
#include <functional>
#include <unordered_map>
#include <format>

// ============================================================================
// Table-driven parser: maps lowercase key → setter on RDPFileData
// ============================================================================

namespace {

using Setter = std::function<void(RDPFileData&, const std::string&)>;

// Helper: parse string with unescaping
Setter str_setter(std::string RDPFileData::* field) {
    return [field](RDPFileData& d, const std::string& v) { d.*field = v; };
}

// Helper: parse "0"/"1" into bool
Setter bool_setter(bool RDPFileData::* field) {
    return [field](RDPFileData& d, const std::string& v) { d.*field = (v == "1"); };
}

// Helper: parse integer with safe fallback
Setter int_setter(int RDPFileData::* field) {
    return [field](RDPFileData& d, const std::string& v) {
        try { d.*field = std::stoi(v); }
        catch (...) { /* keep default */ }
    };
}

const std::unordered_map<std::string, Setter>& rdp_key_map() {
    static const std::unordered_map<std::string, Setter> map = {
        // Basic connection
        {"full address",              str_setter(&RDPFileData::full_address)},
        {"alternate full address",    str_setter(&RDPFileData::alternate_full_address)},
        {"server port",               int_setter(&RDPFileData::server_port)},
        {"username",                  str_setter(&RDPFileData::username)},
        {"domain",                    str_setter(&RDPFileData::domain)},
        // Authentication
        {"prompt for credentials",    bool_setter(&RDPFileData::prompt_for_credentials)},
        {"promptcredentialonce",      bool_setter(&RDPFileData::prompt_credential_once)},
        {"authentication level",      int_setter(&RDPFileData::authentication_level)},
        // Azure AD
        {"targetisaadjoined",         bool_setter(&RDPFileData::target_is_aad_joined)},
        {"enablerdsaadauth",          bool_setter(&RDPFileData::enable_rds_aad_auth)},
        {"aadtenantid",               str_setter(&RDPFileData::aad_tenant_id)},
        // Gateway
        {"gatewayhostname",           str_setter(&RDPFileData::gateway_hostname)},
        {"gatewayusagemethod",        int_setter(&RDPFileData::gateway_usage_method)},
        {"gatewayprofileusagemethod",  int_setter(&RDPFileData::gateway_profile_usage_method)},
        {"gatewaycredentialssource",   int_setter(&RDPFileData::gateway_credentials_source)},
        {"gatewaybrokeringtype",       int_setter(&RDPFileData::gateway_brokering_type)},
        // AVD / WVD
        {"loadbalanceinfo",           str_setter(&RDPFileData::load_balance_info)},
        {"wvd endpoint pool",         str_setter(&RDPFileData::wvd_endpoint_pool)},
        {"armpath",                   str_setter(&RDPFileData::arm_path)},
        {"workspace id",             str_setter(&RDPFileData::workspace_id)},
        {"resourceprovider",          str_setter(&RDPFileData::resource_provider)},
        {"geo",                       str_setter(&RDPFileData::geo)},
        {"diagnosticserviceurl",      str_setter(&RDPFileData::diagnostic_service_url)},
        {"hubdiscoverygeourl",        str_setter(&RDPFileData::hub_discovery_geo_url)},
        {"activityhint",              str_setter(&RDPFileData::activity_hint)},
        // Remote app
        {"remoteapplicationprogram",  str_setter(&RDPFileData::remote_application_program)},
        {"remotedesktopname",         str_setter(&RDPFileData::remote_desktop_name)},
        {"remoteapplicationmode",     int_setter(&RDPFileData::remote_application_mode)},
        // Display
        {"desktopwidth",              int_setter(&RDPFileData::desktop_width)},
        {"desktopheight",             int_setter(&RDPFileData::desktop_height)},
        {"screen mode id",           int_setter(&RDPFileData::screen_mode_id)},
        {"smart sizing",             bool_setter(&RDPFileData::smart_sizing)},
        {"dynamic resolution",       bool_setter(&RDPFileData::dynamic_resolution)},
        {"singlemoninwindowedmode",   int_setter(&RDPFileData::single_mon_in_windowed_mode)},
        // Redirection
        {"redirectclipboard",         bool_setter(&RDPFileData::redirect_clipboard)},
        {"redirectprinters",          bool_setter(&RDPFileData::redirect_printers)},
        {"redirectsmartcards",        bool_setter(&RDPFileData::redirect_smart_cards)},
        {"redirectcomports",          bool_setter(&RDPFileData::redirect_com_ports)},
        {"redirectlocation",          bool_setter(&RDPFileData::redirect_location)},
        {"drivestoredirect",          str_setter(&RDPFileData::drives_to_redirect)},
        {"devicestoredirect",         str_setter(&RDPFileData::devices_to_redirect)},
        {"camerastoredirect",         str_setter(&RDPFileData::cameras_to_redirect)},
        {"usbdevicestoredirect",      str_setter(&RDPFileData::usb_devices_to_redirect)},
        // Audio
        {"audiomode",                 int_setter(&RDPFileData::audio_mode)},
        {"audiocapturemode",          int_setter(&RDPFileData::audio_capture_mode)},
        // Security
        {"signscope",                 str_setter(&RDPFileData::sign_scope)},
        {"signature",                 str_setter(&RDPFileData::signature)},
    };
    return map;
}

} // namespace

std::optional<RDPFileData> RDPFileParser::parse_file(std::string_view filepath) {
    std::ifstream file(std::string{filepath});
    if (!file.is_open()) {
        LOG_ERROR("RDPFileParser", "Failed to open file: " << filepath);
        return std::nullopt;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    std::string content = buffer.str();
    auto result = parse_content(content);
    return result;
}

std::optional<RDPFileData> RDPFileParser::parse_content(std::string_view content) {
    RDPFileData data;
    
    std::istringstream stream{std::string{content}};
    std::string line;
    
    while (std::getline(stream, line)) {
        // Remove trailing CR if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Skip empty lines
        if (line.empty()) continue;
        
        std::string key;
        char type;
        std::string value;
        
        if (!parse_line(line, key, type, value)) {
            continue;  // Skip lines that don't match the format
        }
        
        // Convert key to lowercase for comparison
        std::string key_lower = key;
        std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
        
        // Look up and apply the setter from the table
        const auto& map = rdp_key_map();
        auto it = map.find(key_lower);
        if (it != map.end()) {
            it->second(data, unescape_value(value));
        }
    }
    
    // Validate that we have at least a full address or gateway
    if (data.full_address.empty() && data.gateway_hostname.empty()) {
        LOG_ERROR("RDPFileParser", "No valid connection address found");
        return std::nullopt;
    }
    
    LOG_INFO("RDPFileParser", "Parsed RDP file successfully");
    LOG_DEBUG("RDPFileParser", "  Full address: " << data.full_address);
    LOG_DEBUG("RDPFileParser", "  Gateway: " << data.gateway_hostname);
    LOG_DEBUG("RDPFileParser", "  AVD connection: " << (data.is_avd_connection() ? "yes" : "no"));
    LOG_DEBUG("RDPFileParser", "  AAD auth required: " << (data.requires_aad_auth() ? "yes" : "no"));
    
    return data;
}

bool RDPFileParser::parse_line(const std::string& line, 
                               std::string& key, 
                               char& type, 
                               std::string& value) {
    // RDP file format: key:type:value
    // Where type is 's' for string, 'i' for integer, 'b' for binary
    
    size_t first_colon = line.find(':');
    if (first_colon == std::string::npos) {
        return false;
    }
    
    size_t second_colon = line.find(':', first_colon + 1);
    if (second_colon == std::string::npos) {
        return false;
    }
    
    key = line.substr(0, first_colon);
    
    std::string type_str = line.substr(first_colon + 1, second_colon - first_colon - 1);
    if (type_str.empty()) {
        return false;
    }
    type = type_str[0];
    
    value = line.substr(second_colon + 1);
    
    return true;
}

std::string RDPFileParser::unescape_value(const std::string& value) {
    // RDP files may contain URL-encoded or escaped characters
    // For now, just return as-is since most values are plain text
    return value;
}

std::string RDPFileParser::to_json(const RDPFileData& data) {
    auto esc = [](const std::string& s) { return json_utils::escape_string(s); };
    auto b = [](bool v) -> const char* { return v ? "true" : "false"; };

    return std::format(
        R"({{"full_address":"{}","alternate_full_address":"{}","server_port":{},)" 
        R"("username":"{}","domain":"{}","prompt_for_credentials":{},)" 
        R"("prompt_credential_once":{},"authentication_level":{},)" 
        R"("target_is_aad_joined":{},"enable_rds_aad_auth":{},"aad_tenant_id":"{}",)" 
        R"("gateway_hostname":"{}","gateway_usage_method":{},)" 
        R"("gateway_profile_usage_method":{},"gateway_credentials_source":{},)" 
        R"("gateway_brokering_type":{},)" 
        R"("load_balance_info":"{}","wvd_endpoint_pool":"{}",)" 
        R"("arm_path":"{}","workspace_id":"{}","resource_provider":"{}",)" 
        R"("geo":"{}","diagnostic_service_url":"{}","hub_discovery_geo_url":"{}",)" 
        R"("activity_hint":"{}",)" 
        R"("remote_application_program":"{}","remote_desktop_name":"{}",)" 
        R"("remote_application_mode":{},)" 
        R"("desktop_width":{},"desktop_height":{},"screen_mode_id":{},)" 
        R"("smart_sizing":{},"dynamic_resolution":{},"single_mon_in_windowed_mode":{},)" 
        R"("redirect_clipboard":{},"redirect_printers":{},"redirect_smart_cards":{},)" 
        R"("redirect_com_ports":{},"redirect_location":{},)" 
        R"("drives_to_redirect":"{}","devices_to_redirect":"{}",)" 
        R"("cameras_to_redirect":"{}","usb_devices_to_redirect":"{}",)" 
        R"("audio_mode":{},"audio_capture_mode":{},)" 
        R"("is_avd_connection":{},"uses_gateway":{},"requires_aad_auth":{},)" 
        R"("display_name":"{}"}})",
        esc(data.full_address), esc(data.alternate_full_address), data.server_port,
        esc(data.username), esc(data.domain), b(data.prompt_for_credentials),
        b(data.prompt_credential_once), data.authentication_level,
        b(data.target_is_aad_joined), b(data.enable_rds_aad_auth), esc(data.aad_tenant_id),
        esc(data.gateway_hostname), data.gateway_usage_method,
        data.gateway_profile_usage_method, data.gateway_credentials_source,
        data.gateway_brokering_type,
        esc(data.load_balance_info), esc(data.wvd_endpoint_pool),
        esc(data.arm_path), esc(data.workspace_id), esc(data.resource_provider),
        esc(data.geo), esc(data.diagnostic_service_url), esc(data.hub_discovery_geo_url),
        esc(data.activity_hint),
        esc(data.remote_application_program), esc(data.remote_desktop_name),
        data.remote_application_mode,
        data.desktop_width, data.desktop_height, data.screen_mode_id,
        b(data.smart_sizing), b(data.dynamic_resolution), data.single_mon_in_windowed_mode,
        b(data.redirect_clipboard), b(data.redirect_printers), b(data.redirect_smart_cards),
        b(data.redirect_com_ports), b(data.redirect_location),
        esc(data.drives_to_redirect), esc(data.devices_to_redirect),
        esc(data.cameras_to_redirect), esc(data.usb_devices_to_redirect),
        data.audio_mode, data.audio_capture_mode,
        b(data.is_avd_connection()), b(data.uses_gateway()), b(data.requires_aad_auth()),
        esc(data.get_display_name())
    );
}
