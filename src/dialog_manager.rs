//! Thread-safe dialog synchronization for certificate and authentication prompts.
//!
//! FreeRDP callbacks run on background session threads but UI updates must
//! happen on the WebUI thread. This module bridges the gap using
//! `Mutex` + `Condvar` wait-notify patterns with timeouts.
//!
//! Equivalent to: `src/dialog_manager.cpp` / `dialog_manager.hpp`

// TODO: Implement DialogManager with:
// - Mutex<DialogState> + Condvar
// - handle_certificate_verify() — blocks session thread, signals UI via webui_run()
// - handle_authenticate() — same pattern for credentials dialog
// - on_certificate_response() / on_auth_response() — called from JS handlers, wakes session thread
// - 120-second timeout on wait_for to prevent deadlock
