//! Embedded UI virtual file system.
//!
//! This module includes a build-time generated file containing all React UI
//! assets as compiled-in byte arrays. When the UI has been built before
//! `cargo build`, assets are embedded into the binary for single-file deployment.
//!
//! If no UI was found at build time, the module provides empty stubs and the
//! app falls back to disk-based file serving.

include!(concat!(env!("OUT_DIR"), "/embedded_ui.rs"));
