/**
 * JSON Utility Functions
 * 
 * Helper functions for JSON string manipulation and extraction
 * using the Jansson library.
 */

#ifndef JSON_UTILS_HPP
#define JSON_UTILS_HPP

#include <string>
#include <jansson.h>

namespace json_utils {

/**
 * Escape a string for JSON output
 * Handles special characters like quotes, backslashes, and control characters
 */
inline std::string escape_string(const std::string& s) {
    std::string result;
    result.reserve(s.size() * 2);
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

/**
 * Safely extract a string value from a JSON object
 * @param obj The JSON object to extract from
 * @param key The key to look up
 * @param default_val The default value if key doesn't exist or isn't a string
 * @return The string value or default
 */
inline std::string get_string(json_t* obj, const char* key, const std::string& default_val = "") {
    json_t* val = json_object_get(obj, key);
    if (val && json_is_string(val)) {
        return json_string_value(val);
    }
    return default_val;
}

/**
 * Safely extract an integer value from a JSON object
 * @param obj The JSON object to extract from
 * @param key The key to look up
 * @param default_val The default value if key doesn't exist or isn't an integer
 * @return The integer value or default
 */
inline int get_int(json_t* obj, const char* key, int default_val = 0) {
    json_t* val = json_object_get(obj, key);
    if (val && json_is_integer(val)) {
        return static_cast<int>(json_integer_value(val));
    }
    return default_val;
}

/**
 * Safely extract a boolean value from a JSON object
 * @param obj The JSON object to extract from
 * @param key The key to look up
 * @param default_val The default value if key doesn't exist or isn't a boolean
 * @return The boolean value or default
 */
inline bool get_bool(json_t* obj, const char* key, bool default_val = false) {
    json_t* val = json_object_get(obj, key);
    if (val) {
        if (json_is_boolean(val)) {
            return json_is_true(val);
        }
    }
    return default_val;
}

} // namespace json_utils

#endif // JSON_UTILS_HPP
