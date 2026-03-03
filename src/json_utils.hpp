/**
 * JSON Utility Functions
 * 
 * Helper functions for JSON string manipulation and extraction
 * using the Jansson library.
 */

#pragma once

#include <string>
#include <string_view>
#include <format>
#include <memory>
#include <cstdlib>
#include <jansson.h>

namespace json_utils {

// ============================================================================
// RAII wrappers for Jansson C API
// ============================================================================

/// RAII wrapper for json_t* — automatically calls json_decref on destruction.
struct JsonDeleter {
    void operator()(json_t* p) const noexcept { if (p) json_decref(p); }
};
using JsonPtr = std::unique_ptr<json_t, JsonDeleter>;

/// RAII wrapper for json_dumps() result (char* owned by malloc).
struct FreeDeleter {
    void operator()(char* p) const noexcept { std::free(p); }
};
using MallocPtr = std::unique_ptr<char, FreeDeleter>;

/**
 * Escape a string for JSON output
 * Handles special characters like quotes, backslashes, and control characters
 */
[[nodiscard]] inline std::string escape_string(std::string_view s) {
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
                    result += std::format("\\u{:04x}", static_cast<unsigned char>(c));
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
[[nodiscard]] inline std::string get_string(json_t* obj, const char* key, std::string_view default_val = "") {
    json_t* val = json_object_get(obj, key);
    if (val && json_is_string(val)) {
        const char* strValue = json_string_value(val);
        return strValue;
    }
    return std::string{default_val};
}

/**
 * Safely extract an integer value from a JSON object
 * @param obj The JSON object to extract from
 * @param key The key to look up
 * @param default_val The default value if key doesn't exist or isn't an integer
 * @return The integer value or default
 */
[[nodiscard]] inline int get_int(json_t* obj, const char* key, int default_val = 0) {
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
[[nodiscard]] inline bool get_bool(json_t* obj, const char* key, bool default_val = false) {
    json_t* val = json_object_get(obj, key);
    if (val && json_is_boolean(val)) {
        return json_is_true(val);
    }
    return default_val;
}

} // namespace json_utils


