#![allow(dead_code)]
//! rdpmanager — Native RDP client with React/Fluent UI frontend
//!
//! This is the Rust port of the C++23 rdpmanager application.
//! The architecture mirrors the original:
//!
//! - `gui::main_window` — Main WebUI window and application lifecycle
//! - `gui::aad_auth_handler` — Azure AD OAuth popup authentication
//! - `rdp_launcher` — FreeRDP session management and threading
//! - `config_manager` — SQLite persistence (connections, folders, tokens)
//! - `dialog_manager` — Thread-safe cert/auth dialog synchronization
//! - `feed_discovery` — WVD/AVD feed discovery and import
//! - `js_handlers` — WebUI JavaScript ↔ Rust binding layer (30 bindings)
//! - `rdp_file_parser` — .rdp file import/parsing
//! - `types` — Shared data structures and callback types
//! - `utils` — URL encoding, base64url, UUID, JWT helpers

mod config_manager;
mod dialog_manager;
mod embedded_ui;
mod feed_discovery;
mod file_dialogs;
mod gui;
mod js_handlers;
mod rdp_file_parser;
mod rdp_launcher;
mod session_manager;
mod types;
mod utils;
mod window_embedding;

use gui::aad_auth_handler::AADAuthHandler;
use gui::main_window::MainWindow;
use log::{error, info};

// ---------------------------------------------------------------------------
// CLI argument parsing (mirrors C++ main.cpp argument handling)
// ---------------------------------------------------------------------------

struct AppConfig {
    debug_port: Option<u16>,
    log_level: Option<String>,
    manual_code_flow: bool,
    aad_debug: bool,
}

fn parse_args() -> AppConfig {
    let mut config = AppConfig {
        debug_port: None,
        log_level: None,
        manual_code_flow: false,
        aad_debug: false,
    };
    for arg in std::env::args().skip(1) {
        if let Some(port) = arg.strip_prefix("--debug-port=") {
            match port.parse::<u16>() {
                Ok(p) => config.debug_port = Some(p),
                Err(_) => eprintln!("Invalid debug port: {port}"),
            }
        } else if let Some(level) = arg.strip_prefix("--log-level=") {
            config.log_level = Some(level.to_string());
        } else if arg == "-USE_MANUAL_CODE_FLOW" || arg == "--manual-code-flow" {
            config.manual_code_flow = true;
        } else if arg == "--aad-dbg" {
            config.aad_debug = true;
        }
    }
    config
}

// ---------------------------------------------------------------------------
// Linux TLS backend configuration (mirrors C++ configure_webview_tls_backend)
// ---------------------------------------------------------------------------

#[cfg(target_os = "linux")]
fn configure_webview_tls_backend() {
    use std::path::Path;

    const MODULE_DIRS: &[&str] = &[
        "/usr/lib/x86_64-linux-gnu/gio/modules",
        "/usr/lib/aarch64-linux-gnu/gio/modules",
        "/usr/lib/gio/modules",
        "/usr/lib64/gio/modules",
    ];

    const TLS_MODULES: &[&str] = &["libgiognutls.so", "libgioopenssl.so"];

    let has_tls_module = |dir: &str| -> bool {
        let base = Path::new(dir);
        TLS_MODULES.iter().any(|m| base.join(m).is_file())
    };

    // Collect directories that exist and contain a TLS module
    let tls_dirs: Vec<&str> = MODULE_DIRS
        .iter()
        .copied()
        .filter(|d| Path::new(d).is_dir() && has_tls_module(d))
        .collect();

    if tls_dirs.is_empty() {
        log::warn!(
            "No GIO TLS backend module found. \
             Install glib-networking to enable HTTPS in embedded WebView."
        );
        return;
    }

    // Merge into GIO_EXTRA_MODULES, avoiding duplicates
    let current = std::env::var("GIO_EXTRA_MODULES").unwrap_or_default();
    let existing: Vec<&str> = if current.is_empty() {
        Vec::new()
    } else {
        current.split(':').collect()
    };

    let mut merged = current.clone();
    let mut updated = false;
    for path in &tls_dirs {
        if !existing.contains(path) {
            if !merged.is_empty() {
                merged.push(':');
            }
            merged.push_str(path);
            updated = true;
        }
    }

    if updated {
        // SAFETY: called at startup before spawning threads
        unsafe { std::env::set_var("GIO_EXTRA_MODULES", &merged) };
        log::info!("GIO_EXTRA_MODULES={merged}");
    }

    if std::env::var("GIO_MODULE_DIR").is_err() {
        unsafe { std::env::set_var("GIO_MODULE_DIR", tls_dirs[0]) };
        log::info!("GIO_MODULE_DIR={}", tls_dirs[0]);
    }
}

fn main() {
    // Parse CLI arguments before logger init so --log-level takes effect
    let config = parse_args();

    // --aad-dbg implies debug-level logging (matches C++ behaviour)
    let default_filter = if config.aad_debug {
        "debug"
    } else {
        config.log_level.as_deref().unwrap_or("info")
    };

    // Allow RUST_LOG env var to override CLI flag
    if std::env::var("RUST_LOG").is_err() {
        // SAFETY: called at startup before spawning threads
        unsafe { std::env::set_var("RUST_LOG", default_filter) };
    }

    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or(default_filter))
        .format_timestamp_millis()
        .init();

    info!("rdpmanager v{} starting", env!("CARGO_PKG_VERSION"));

    // Apply AAD flags
    if config.manual_code_flow {
        AADAuthHandler::enable_manual_code_flow();
    }
    if config.aad_debug {
        AADAuthHandler::enable_debug();
    }

    // Configure TLS backend for WebKit on Linux
    #[cfg(target_os = "linux")]
    configure_webview_tls_backend();

    // Create and initialize the main window
    let window = MainWindow::new();

    // Configure browser DevTools debugging if requested
    if let Some(port) = config.debug_port {
        window.set_debug_port(port);
    }

    if !window.initialize() {
        error!("Failed to initialize — exiting");
        std::process::exit(1);
    }

    // Show the UI window
    if !window.show() {
        error!("Failed to show window — exiting");
        std::process::exit(1);
    }

    info!("Window opened — waiting for close");

    // Block until the window is closed
    MainWindow::wait();

    info!("rdpmanager exiting");
}
