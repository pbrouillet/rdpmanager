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
mod gui;
mod js_handlers;
mod rdp_file_parser;
mod rdp_launcher;
mod session_manager;
mod types;
mod utils;
mod window_embedding;

use gui::main_window::MainWindow;
use log::{error, info};

fn main() {
    // Initialize logging
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"))
        .format_timestamp_millis()
        .init();

    info!("rdpmanager v{} starting", env!("CARGO_PKG_VERSION"));

    // Create and initialize the main window
    let window = MainWindow::new();

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
