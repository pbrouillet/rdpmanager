//! Raw FFI bindings to the [WebUI](https://github.com/webui-dev/webui) C library.
//!
//! This crate vendors WebUI as a git submodule, compiles it from source
//! as a static library, and exposes the generated bindings.
//!
//! # Usage
//!
//! This is a `-sys` crate — use the raw C functions directly or build a
//! safe wrapper on top. All WebUI functions are prefixed with `webui_`.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
