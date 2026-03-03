#pragma once

/**
 * RDP File Parser
 * 
 * Parses .rdp and .rdpw files (Microsoft Remote Desktop Connection files)
 * and extracts connection parameters including Azure Virtual Desktop (AVD)
 * and Microsoft Dev Box settings.
 */

#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>

/**
 * Parsed RDP file data
 * Contains all supported .rdp/.rdpw file properties
 */
struct RDPFileData {
    // Basic connection
    std::string full_address;              // full address:s:
    std::string alternate_full_address;    // alternate full address:s:
    int server_port = 3389;                // server port:i:
    
    // Authentication
    std::string username;                  // username:s:
    std::string domain;                    // domain:s:
    bool prompt_for_credentials = true;    // prompt for credentials:i:
    bool prompt_credential_once = false;   // promptcredentialonce:i:
    int authentication_level = 2;          // authentication level:i:
    
    // Azure AD / AVD Authentication
    bool target_is_aad_joined = false;     // targetisaadjoined:i:
    bool enable_rds_aad_auth = false;      // enablerdsaadauth:i:
    std::string aad_tenant_id;             // aadtenantid:s:
    
    // RD Gateway settings
    std::string gateway_hostname;          // gatewayhostname:s:
    int gateway_usage_method = 0;          // gatewayusagemethod:i: (0=none, 1=always, 2=detect)
    int gateway_profile_usage_method = 0;  // gatewayprofileusagemethod:i:
    int gateway_credentials_source = 0;    // gatewaycredentialssource:i: (0=any, 1=smartcard, 4=ask)
    int gateway_brokering_type = 0;        // gatewaybrokeringtype:i:
    
    // AVD/WVD specific
    std::string load_balance_info;         // loadbalanceinfo:s:
    std::string wvd_endpoint_pool;         // wvd endpoint pool:s:
    std::string arm_path;                  // armpath:s:
    std::string workspace_id;              // workspace id:s:
    std::string resource_provider;         // resourceprovider:s:
    std::string geo;                       // geo:s:
    std::string diagnostic_service_url;    // diagnosticserviceurl:s:
    std::string hub_discovery_geo_url;     // hubdiscoverygeourl:s:
    std::string activity_hint;             // activityhint:s:
    
    // Remote Application mode
    std::string remote_application_program; // remoteapplicationprogram:s:
    std::string remote_desktop_name;        // remotedesktopname:s:
    int remote_application_mode = 0;        // remoteapplicationmode:i:
    
    // Display settings
    int desktop_width = 0;                 // desktopwidth:i:
    int desktop_height = 0;                // desktopheight:i:
    int screen_mode_id = 1;                // screen mode id:i: (1=window, 2=fullscreen)
    bool smart_sizing = false;             // smart sizing:i:
    bool dynamic_resolution = false;       // dynamic resolution:i:
    int single_mon_in_windowed_mode = 0;   // singlemoninwindowedmode:i:
    
    // Redirection settings
    bool redirect_clipboard = true;        // redirectclipboard:i:
    bool redirect_printers = false;        // redirectprinters:i:
    bool redirect_smart_cards = false;     // redirectsmartcards:i:
    bool redirect_com_ports = false;       // redirectcomports:i:
    bool redirect_location = false;        // redirectlocation:i:
    std::string drives_to_redirect;        // drivestoredirect:s:
    std::string devices_to_redirect;       // devicestoredirect:s:
    std::string cameras_to_redirect;       // camerastoredirect:s:
    std::string usb_devices_to_redirect;   // usbdevicestoredirect:s:
    
    // Audio settings
    int audio_mode = 0;                    // audiomode:i: (0=bring to local, 1=play on remote, 2=disable)
    int audio_capture_mode = 0;            // audiocapturemode:i:
    
    // Security/signature (for validation)
    std::string sign_scope;                // signscope:s:
    std::string signature;                 // signature:s:
    
    // Helper methods
    [[nodiscard]] constexpr bool is_avd_connection() const {
        return !wvd_endpoint_pool.empty() || !arm_path.empty() || 
               !load_balance_info.empty() || enable_rds_aad_auth;
    }
    
    [[nodiscard]] constexpr bool uses_gateway() const {
        return !gateway_hostname.empty() && gateway_usage_method > 0;
    }
    
    [[nodiscard]] constexpr bool requires_aad_auth() const {
        return target_is_aad_joined || enable_rds_aad_auth;
    }
    
    // Get the effective hostname (gateway for AVD, full address otherwise)
    [[nodiscard]] std::string get_connection_host() const {
        if (is_avd_connection() && uses_gateway()) {
            // For AVD, we connect through the gateway
            return gateway_hostname;
        }
        return full_address;
    }
    
    // Get the display name for UI
    [[nodiscard]] std::string get_display_name() const {
        if (!remote_desktop_name.empty()) {
            return remote_desktop_name;
        }
        return full_address;
    }
};

/**
 * RDP File Parser class
 */
class RDPFileParser {
public:
    /**
     * Parse an RDP file from a file path
     * @param filepath Path to the .rdp or .rdpw file
     * @return Parsed RDP file data, or nullopt on failure
     */
    [[nodiscard]] static std::optional<RDPFileData> parse_file(std::string_view filepath);
    
    /**
     * Parse RDP file content from a string
     * @param content The RDP file content as a string
     * @return Parsed RDP file data, or nullopt on failure
     */
    [[nodiscard]] static std::optional<RDPFileData> parse_content(std::string_view content);
    
    /**
     * Get a JSON representation of the parsed RDP file data
     * @param data The parsed RDP file data
     * @return JSON string representation
     */
    [[nodiscard]] static std::string to_json(const RDPFileData& data);
    
private:
    /**
     * Parse a single line of an RDP file
     * @param line The line to parse
     * @param key Output parameter for the key
     * @param type Output parameter for the type ('s' for string, 'i' for integer)
     * @param value Output parameter for the value
     * @return true if successfully parsed, false otherwise
     */
    static bool parse_line(const std::string& line, 
                           std::string& key, 
                           char& type, 
                           std::string& value);
    
    /**
     * Unescape special characters in RDP values
     */
    static std::string unescape_value(const std::string& value);
};
