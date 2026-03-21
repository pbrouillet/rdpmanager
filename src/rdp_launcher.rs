//! FreeRDP session management and threading.
//!
//! Spawns FreeRDP sessions on background threads, registers callbacks
//! for certificate verification, authentication, and AAD OAuth token
//! exchange. Uses a global session map for callback trampolines.
//!
//! Equivalent to: `src/rdp_launcher.cpp` / `rdp_launcher.hpp`

// TODO: Implement:
// - RDPConnectionParams struct (30+ fields matching ConnectionProfile)
// - RDPSession struct (thread handle, state, callbacks, freerdp context)
// - RDPLauncher struct (session list, callback setters)
// - Global g_session_map: Mutex<HashMap<*mut freerdp, Arc<Mutex<RDPSession>>>>
// - Static callback trampolines (verify_certificate, authenticate, get_access_token)
// - Session thread function (context_new → apply_settings → install_callbacks → start → wait → stop → free)
// - AAD token exchange flow with variadic get_access_token_callback
// - Token cache integration (lookup before auth, store after)
