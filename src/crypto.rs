//! AES-256-GCM encryption for stored passwords.
//!
//! Passwords are encrypted with a per-database random key before storage.
//! The encrypted payload is base64-encoded: `nonce (12 bytes) || ciphertext || tag (16 bytes)`.

use aes_gcm::aead::{Aead, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use base64::Engine;
use rand::rngs::OsRng;
use rand::RngCore;

const NONCE_LEN: usize = 12;

/// Generate a random 256-bit encryption key.
pub fn generate_key() -> [u8; 32] {
    let mut key = [0u8; 32];
    OsRng.fill_bytes(&mut key);
    key
}

/// Encrypt a plaintext string with AES-256-GCM.
/// Returns base64-encoded `nonce || ciphertext || tag`.
pub fn encrypt(key: &[u8; 32], plaintext: &str) -> Result<String, String> {
    let cipher = Aes256Gcm::new_from_slice(key).map_err(|e| format!("cipher init: {e}"))?;

    let mut nonce_bytes = [0u8; NONCE_LEN];
    OsRng.fill_bytes(&mut nonce_bytes);
    let nonce = Nonce::from_slice(&nonce_bytes);

    let ciphertext = cipher
        .encrypt(nonce, plaintext.as_bytes())
        .map_err(|e| format!("encrypt: {e}"))?;

    let mut payload = Vec::with_capacity(NONCE_LEN + ciphertext.len());
    payload.extend_from_slice(&nonce_bytes);
    payload.extend_from_slice(&ciphertext);

    Ok(base64::engine::general_purpose::STANDARD.encode(&payload))
}

/// Decrypt a base64-encoded `nonce || ciphertext || tag` payload.
pub fn decrypt(key: &[u8; 32], encoded: &str) -> Result<String, String> {
    let payload = base64::engine::general_purpose::STANDARD
        .decode(encoded)
        .map_err(|e| format!("base64 decode: {e}"))?;

    if payload.len() < NONCE_LEN + 1 {
        return Err("ciphertext too short".into());
    }

    let (nonce_bytes, ciphertext) = payload.split_at(NONCE_LEN);
    let nonce = Nonce::from_slice(nonce_bytes);

    let cipher = Aes256Gcm::new_from_slice(key).map_err(|e| format!("cipher init: {e}"))?;

    let plaintext = cipher
        .decrypt(nonce, ciphertext)
        .map_err(|e| format!("decrypt: {e}"))?;

    String::from_utf8(plaintext).map_err(|e| format!("utf8: {e}"))
}

/// Encode a 32-byte key as hex string (for SQLite storage).
pub fn key_to_hex(key: &[u8; 32]) -> String {
    key.iter().map(|b| format!("{b:02x}")).collect()
}

/// Decode a hex string back to a 32-byte key.
pub fn key_from_hex(hex: &str) -> Result<[u8; 32], String> {
    if hex.len() != 64 {
        return Err(format!(
            "invalid key length: {} (expected 64 hex chars)",
            hex.len()
        ));
    }
    let mut key = [0u8; 32];
    for (i, chunk) in hex.as_bytes().chunks(2).enumerate() {
        let s = std::str::from_utf8(chunk).map_err(|e| format!("hex parse: {e}"))?;
        key[i] = u8::from_str_radix(s, 16).map_err(|e| format!("hex parse: {e}"))?;
    }
    Ok(key)
}
