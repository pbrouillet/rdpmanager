//! Utility functions: URL encoding, base64url, UUID, JWT helpers.
//!
//! Equivalent to: `src/utils.hpp`

/// URL-decode a percent-encoded string.
pub fn url_decode(input: &str) -> String {
    url::form_urlencoded::parse(input.as_bytes())
        .map(|(k, v)| {
            if v.is_empty() {
                k.into_owned()
            } else {
                format!("{k}={v}")
            }
        })
        .collect::<Vec<_>>()
        .join("&")
}

/// URL-encode a string.
pub fn url_encode(input: &str) -> String {
    url::form_urlencoded::byte_serialize(input.as_bytes()).collect()
}

/// Base64url-encode bytes (no padding, URL-safe alphabet).
pub fn base64url_encode(data: &[u8]) -> String {
    use base64::engine::general_purpose::URL_SAFE_NO_PAD;
    use base64::Engine;
    URL_SAFE_NO_PAD.encode(data)
}

/// Base64url-decode a string.
pub fn base64url_decode(input: &str) -> Result<Vec<u8>, base64::DecodeError> {
    use base64::engine::general_purpose::URL_SAFE_NO_PAD;
    use base64::Engine;
    URL_SAFE_NO_PAD.decode(input)
}

/// Generate a UUID v4 string.
pub fn generate_uuid() -> String {
    uuid::Uuid::new_v4().to_string()
}

/// Generate a PKCE code verifier (128 URL-safe random characters).
pub fn generate_code_verifier() -> String {
    use base64::engine::general_purpose::URL_SAFE_NO_PAD;
    use base64::Engine;

    let random_bytes: Vec<u8> = (0..96).map(|_| rand_byte()).collect();
    URL_SAFE_NO_PAD.encode(&random_bytes)
}

/// Extract a field value from a JWT token (without verification).
pub fn jwt_extract_field(token: &str, field: &str) -> Option<String> {
    let parts: Vec<&str> = token.split('.').collect();
    if parts.len() < 2 {
        return None;
    }
    let payload = base64url_decode(parts[1]).ok()?;
    let json: serde_json::Value = serde_json::from_slice(&payload).ok()?;
    json.get(field)?.as_str().map(|s| s.to_string())
}

/// Extract the "exp" claim from a JWT as epoch seconds.
pub fn jwt_extract_expiration(token: &str) -> Option<i64> {
    let parts: Vec<&str> = token.split('.').collect();
    if parts.len() < 2 {
        return None;
    }
    let payload = base64url_decode(parts[1]).ok()?;
    let json: serde_json::Value = serde_json::from_slice(&payload).ok()?;
    json.get("exp")?.as_i64()
}

/// HTML entity decode (basic: &lt; &gt; &amp; &quot; &apos;).
pub fn html_decode(input: &str) -> String {
    input
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&amp;", "&")
        .replace("&quot;", "\"")
        .replace("&apos;", "'")
}

// Simple random byte generator (for code verifier)
fn rand_byte() -> u8 {
    // Use a simple approach; in production, consider `rand` crate
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};
    use std::time::SystemTime;

    let mut hasher = DefaultHasher::new();
    SystemTime::now().hash(&mut hasher);
    std::thread::current().id().hash(&mut hasher);
    (hasher.finish() & 0xFF) as u8
}
