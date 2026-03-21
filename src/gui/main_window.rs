//! Main WebUI window and application lifecycle.
//!
//! Owns all manager instances (RDPLauncher, ConfigManager, DialogManager,
//! AADAuthHandler, FeedDiscoveryManager, JSHandlers) and manages the WebUI
//! window that serves the React frontend.
//!
//! Equivalent to: `src/gui/main_window.cpp` / `main_window.hpp`

// TODO: Implement MainWindow struct with:
// - webui window handle (from webui-sys)
// - RDPLauncher, ConfigManager, DialogManager, AADAuthHandler, FeedDiscoveryManager
// - JSHandlers binding registration
// - Window lifecycle: initialize() → show() → webui_wait() → exit
// - VFS (embedded UI) vs disk file serving
// - Debug port configuration for Chrome DevTools
