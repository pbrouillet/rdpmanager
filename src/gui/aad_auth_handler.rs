//! Azure AD / Entra ID OAuth popup authentication handler.
//!
//! Opens a secondary WebUI window for Microsoft OAuth login flows.
//! Supports three modes:
//! - Standard WebView popup (default)
//! - UI manual code flow (per-connection setting)
//! - CLI manual code flow (global flag)
//!
//! Equivalent to: `src/gui/aad_auth_handler.cpp` / `aad_auth_handler.hpp`

// TODO: Implement AADAuthHandler with:
// - OAuth popup window (webui secondary window)
// - URL rewriting for cross-platform redirect URIs
// - Navigation interception to capture authorization code
// - mutex + condvar wait for auth completion (5 min timeout)
// - Manual code flow fallback
