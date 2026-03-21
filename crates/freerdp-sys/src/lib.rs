//! Raw FFI bindings to [FreeRDP3](https://github.com/FreeRDP/FreeRDP).
//!
//! This crate vendors FreeRDP as a git submodule, builds it from source
//! via CMake as a static library, and exposes generated Rust bindings.
//!
//! # Build Requirements
//!
//! On Linux, the following system packages are needed:
//! - `libssl-dev` — OpenSSL headers
//! - `libx11-dev` and friends — X11 development headers
//! - `libavcodec-dev`, `libavutil-dev`, etc. — FFmpeg development headers
//! - `cmake`, `ninja-build` — build tools
//!
//! # Usage
//!
//! This is a `-sys` crate — use the raw C functions directly or build a
//! safe wrapper on top.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
