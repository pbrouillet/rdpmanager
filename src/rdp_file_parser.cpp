/**
 * RDP File Parser Implementation
 * 
 * Parses Microsoft Remote Desktop Connection (.rdp/.rdpw) files.
 */

#include "rdp_file_parser.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cctype>

std::optional<RDPFileData> RDPFileParser::parse_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[RDPFileParser] Failed to open file: " << filepath << std::endl;
        return std::nullopt;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    std::string content = buffer.str();
    auto result = parse_content(content);
    return result;
}

std::optional<RDPFileData> RDPFileParser::parse_content(const std::string& content) {
    RDPFileData data;
    
    std::istringstream stream(content);
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
        
        // Parse based on key
        if (key_lower == "full address") {
            data.full_address = unescape_value(value);
        }
        else if (key_lower == "alternate full address") {
            data.alternate_full_address = unescape_value(value);
        }
        else if (key_lower == "server port") {
            data.server_port = std::stoi(value);
        }
        else if (key_lower == "username") {
            data.username = unescape_value(value);
        }
        else if (key_lower == "domain") {
            data.domain = unescape_value(value);
        }
        else if (key_lower == "prompt for credentials") {
            data.prompt_for_credentials = (value == "1");
        }
        else if (key_lower == "promptcredentialonce") {
            data.prompt_credential_once = (value == "1");
        }
        else if (key_lower == "authentication level") {
            data.authentication_level = std::stoi(value);
        }
        else if (key_lower == "targetisaadjoined") {
            data.target_is_aad_joined = (value == "1");
        }
        else if (key_lower == "enablerdsaadauth") {
            data.enable_rds_aad_auth = (value == "1");
        }
        else if (key_lower == "aadtenantid") {
            data.aad_tenant_id = unescape_value(value);
        }
        else if (key_lower == "gatewayhostname") {
            data.gateway_hostname = unescape_value(value);
        }
        else if (key_lower == "gatewayusagemethod") {
            data.gateway_usage_method = std::stoi(value);
        }
        else if (key_lower == "gatewayprofileusagemethod") {
            data.gateway_profile_usage_method = std::stoi(value);
        }
        else if (key_lower == "gatewaycredentialssource") {
            data.gateway_credentials_source = std::stoi(value);
        }
        else if (key_lower == "gatewaybrokeringtype") {
            data.gateway_brokering_type = std::stoi(value);
        }
        else if (key_lower == "loadbalanceinfo") {
            data.load_balance_info = unescape_value(value);
        }
        else if (key_lower == "wvd endpoint pool") {
            data.wvd_endpoint_pool = unescape_value(value);
        }
        else if (key_lower == "armpath") {
            data.arm_path = unescape_value(value);
        }
        else if (key_lower == "workspace id") {
            data.workspace_id = unescape_value(value);
        }
        else if (key_lower == "resourceprovider") {
            data.resource_provider = unescape_value(value);
        }
        else if (key_lower == "geo") {
            data.geo = unescape_value(value);
        }
        else if (key_lower == "diagnosticserviceurl") {
            data.diagnostic_service_url = unescape_value(value);
        }
        else if (key_lower == "hubdiscoverygeourl") {
            data.hub_discovery_geo_url = unescape_value(value);
        }
        else if (key_lower == "activityhint") {
            data.activity_hint = unescape_value(value);
        }
        else if (key_lower == "remoteapplicationprogram") {
            data.remote_application_program = unescape_value(value);
        }
        else if (key_lower == "remotedesktopname") {
            data.remote_desktop_name = unescape_value(value);
        }
        else if (key_lower == "remoteapplicationmode") {
            data.remote_application_mode = std::stoi(value);
        }
        else if (key_lower == "desktopwidth") {
            data.desktop_width = std::stoi(value);
        }
        else if (key_lower == "desktopheight") {
            data.desktop_height = std::stoi(value);
        }
        else if (key_lower == "screen mode id") {
            data.screen_mode_id = std::stoi(value);
        }
        else if (key_lower == "smart sizing") {
            data.smart_sizing = (value == "1");
        }
        else if (key_lower == "dynamic resolution") {
            data.dynamic_resolution = (value == "1");
        }
        else if (key_lower == "singlemoninwindowedmode") {
            data.single_mon_in_windowed_mode = std::stoi(value);
        }
        else if (key_lower == "redirectclipboard") {
            data.redirect_clipboard = (value == "1");
        }
        else if (key_lower == "redirectprinters") {
            data.redirect_printers = (value == "1");
        }
        else if (key_lower == "redirectsmartcards") {
            data.redirect_smart_cards = (value == "1");
        }
        else if (key_lower == "redirectcomports") {
            data.redirect_com_ports = (value == "1");
        }
        else if (key_lower == "redirectlocation") {
            data.redirect_location = (value == "1");
        }
        else if (key_lower == "drivestoredirect") {
            data.drives_to_redirect = unescape_value(value);
        }
        else if (key_lower == "devicestoredirect") {
            data.devices_to_redirect = unescape_value(value);
        }
        else if (key_lower == "camerastoredirect") {
            data.cameras_to_redirect = unescape_value(value);
        }
        else if (key_lower == "usbdevicestoredirect") {
            data.usb_devices_to_redirect = unescape_value(value);
        }
        else if (key_lower == "audiomode") {
            data.audio_mode = std::stoi(value);
        }
        else if (key_lower == "audiocapturemode") {
            data.audio_capture_mode = std::stoi(value);
        }
        else if (key_lower == "signscope") {
            data.sign_scope = unescape_value(value);
        }
        else if (key_lower == "signature") {
            data.signature = unescape_value(value);
        }
    }
    
    // Validate that we have at least a full address or gateway
    if (data.full_address.empty() && data.gateway_hostname.empty()) {
        std::cerr << "[RDPFileParser] No valid connection address found" << std::endl;
        return std::nullopt;
    }
    
    std::cout << "[RDPFileParser] Parsed RDP file successfully" << std::endl;
    std::cout << "[RDPFileParser]   Full address: " << data.full_address << std::endl;
    std::cout << "[RDPFileParser]   Gateway: " << data.gateway_hostname << std::endl;
    std::cout << "[RDPFileParser]   AVD connection: " << (data.is_avd_connection() ? "yes" : "no") << std::endl;
    std::cout << "[RDPFileParser]   AAD auth required: " << (data.requires_aad_auth() ? "yes" : "no") << std::endl;
    
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

std::string RDPFileParser::escape_json(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 10);
    
    for (char c : str) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control characters - output as unicode escape
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    
    return result;
}

std::string RDPFileParser::to_json(const RDPFileData& data) {
    std::ostringstream json;
    
    json << "{";
    
    // Basic connection
    json << "\"full_address\":\"" << escape_json(data.full_address) << "\",";
    json << "\"alternate_full_address\":\"" << escape_json(data.alternate_full_address) << "\",";
    json << "\"server_port\":" << data.server_port << ",";
    
    // Authentication
    json << "\"username\":\"" << escape_json(data.username) << "\",";
    json << "\"domain\":\"" << escape_json(data.domain) << "\",";
    json << "\"prompt_for_credentials\":" << (data.prompt_for_credentials ? "true" : "false") << ",";
    json << "\"prompt_credential_once\":" << (data.prompt_credential_once ? "true" : "false") << ",";
    json << "\"authentication_level\":" << data.authentication_level << ",";
    
    // Azure AD
    json << "\"target_is_aad_joined\":" << (data.target_is_aad_joined ? "true" : "false") << ",";
    json << "\"enable_rds_aad_auth\":" << (data.enable_rds_aad_auth ? "true" : "false") << ",";
    json << "\"aad_tenant_id\":\"" << escape_json(data.aad_tenant_id) << "\",";
    
    // Gateway
    json << "\"gateway_hostname\":\"" << escape_json(data.gateway_hostname) << "\",";
    json << "\"gateway_usage_method\":" << data.gateway_usage_method << ",";
    json << "\"gateway_profile_usage_method\":" << data.gateway_profile_usage_method << ",";
    json << "\"gateway_credentials_source\":" << data.gateway_credentials_source << ",";
    json << "\"gateway_brokering_type\":" << data.gateway_brokering_type << ",";
    
    // AVD specific
    json << "\"load_balance_info\":\"" << escape_json(data.load_balance_info) << "\",";
    json << "\"wvd_endpoint_pool\":\"" << escape_json(data.wvd_endpoint_pool) << "\",";
    json << "\"arm_path\":\"" << escape_json(data.arm_path) << "\",";
    json << "\"workspace_id\":\"" << escape_json(data.workspace_id) << "\",";
    json << "\"resource_provider\":\"" << escape_json(data.resource_provider) << "\",";
    json << "\"geo\":\"" << escape_json(data.geo) << "\",";
    json << "\"diagnostic_service_url\":\"" << escape_json(data.diagnostic_service_url) << "\",";
    json << "\"hub_discovery_geo_url\":\"" << escape_json(data.hub_discovery_geo_url) << "\",";
    json << "\"activity_hint\":\"" << escape_json(data.activity_hint) << "\",";
    
    // Remote app
    json << "\"remote_application_program\":\"" << escape_json(data.remote_application_program) << "\",";
    json << "\"remote_desktop_name\":\"" << escape_json(data.remote_desktop_name) << "\",";
    json << "\"remote_application_mode\":" << data.remote_application_mode << ",";
    
    // Display
    json << "\"desktop_width\":" << data.desktop_width << ",";
    json << "\"desktop_height\":" << data.desktop_height << ",";
    json << "\"screen_mode_id\":" << data.screen_mode_id << ",";
    json << "\"smart_sizing\":" << (data.smart_sizing ? "true" : "false") << ",";
    json << "\"dynamic_resolution\":" << (data.dynamic_resolution ? "true" : "false") << ",";
    json << "\"single_mon_in_windowed_mode\":" << data.single_mon_in_windowed_mode << ",";
    
    // Redirection
    json << "\"redirect_clipboard\":" << (data.redirect_clipboard ? "true" : "false") << ",";
    json << "\"redirect_printers\":" << (data.redirect_printers ? "true" : "false") << ",";
    json << "\"redirect_smart_cards\":" << (data.redirect_smart_cards ? "true" : "false") << ",";
    json << "\"redirect_com_ports\":" << (data.redirect_com_ports ? "true" : "false") << ",";
    json << "\"redirect_location\":" << (data.redirect_location ? "true" : "false") << ",";
    json << "\"drives_to_redirect\":\"" << escape_json(data.drives_to_redirect) << "\",";
    json << "\"devices_to_redirect\":\"" << escape_json(data.devices_to_redirect) << "\",";
    json << "\"cameras_to_redirect\":\"" << escape_json(data.cameras_to_redirect) << "\",";
    json << "\"usb_devices_to_redirect\":\"" << escape_json(data.usb_devices_to_redirect) << "\",";
    
    // Audio
    json << "\"audio_mode\":" << data.audio_mode << ",";
    json << "\"audio_capture_mode\":" << data.audio_capture_mode << ",";
    
    // Helper computed properties
    json << "\"is_avd_connection\":" << (data.is_avd_connection() ? "true" : "false") << ",";
    json << "\"uses_gateway\":" << (data.uses_gateway() ? "true" : "false") << ",";
    json << "\"requires_aad_auth\":" << (data.requires_aad_auth() ? "true" : "false") << ",";
    json << "\"display_name\":\"" << escape_json(data.get_display_name()) << "\"";
    
    json << "}";
    
    std::string jsonString = json.str();
    return jsonString;
}
