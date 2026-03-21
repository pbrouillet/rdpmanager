// freerdp-sys build script
//
// 1. Applies the aad-fallback-parse patch to the vendored FreeRDP source
// 2. Builds FreeRDP as a static library via CMake (matching the project's meson.build defines)
// 3. Generates Rust FFI bindings from FreeRDP/WinPR headers via `bindgen`

use std::{env, path::PathBuf, process::Command};

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let freerdp_dir = manifest_dir.join("freerdp");

    // --- Apply local AAD patch ---
    let patch = manifest_dir
        .join("patches")
        .join("aad-fallback-parse.patch");
    if patch.exists() && freerdp_dir.join(".git").exists() {
        let check = Command::new("git")
            .args(["apply", "--check"])
            .arg(&patch)
            .current_dir(&freerdp_dir)
            .output();

        if let Ok(output) = check {
            if output.status.success() {
                let _ = Command::new("git")
                    .args(["apply", "--3way"])
                    .arg(&patch)
                    .current_dir(&freerdp_dir)
                    .status();
                println!("cargo:warning=Applied aad-fallback-parse.patch to FreeRDP");
            }
        }
    }

    // --- Build FreeRDP via CMake ---
    // These defines match the current meson.build CMake configuration exactly.
    let dst = cmake::Config::new(&freerdp_dir)
        // Static libraries
        .define("BUILD_SHARED_LIBS", "OFF")
        // Client only — no server, proxy, shadow, samples
        .define("WITH_CLIENT", "ON")
        .define("WITH_SERVER", "OFF")
        .define("WITH_SAMPLE", "OFF")
        .define("WITH_PLATFORM_SERVER", "OFF")
        .define("WITH_PROXY", "OFF")
        .define("WITH_SHADOW", "OFF")
        .define("WITH_MANPAGES", "OFF")
        // Disable auth mechanisms we don't need
        .define("WITH_GSSAPI", "OFF")
        .define("WITH_KRB5", "OFF")
        .define("WITH_PKCS11", "OFF")
        // Enable X11 client frontend (provides RdpClientEntry / xfreerdp-client lib)
        .define("WITH_X11", "ON")
        .define("WITH_CLIENT_INTERFACE", "ON")
        // Disable alternative client frontends
        .define("WITH_CLIENT_SDL", "OFF")
        .define("WITH_CLIENT_WAYLAND", "OFF")
        .define("WITH_WAYLAND", "OFF")
        // Disable features that bring in extra dependencies
        .define("WITH_FUSE", "OFF")
        .define("CHANNEL_REMDESK", "OFF")
        // Enable Azure AD authentication
        .define("WITH_AAD", "ON")
        // Enable H264 video codec support (via FFmpeg)
        .define("WITH_FFMPEG", "ON")
        .define("WITH_VIDEO_FFMPEG", "ON")
        .define("WITH_DSP_FFMPEG", "ON")
        .define("WITH_OPENH264", "OFF")
        // Use built-in Unicode converter (avoids ~35MB ICU runtime dependency)
        .define("WITH_UNICODE_BUILTIN", "ON")
        // Enable compression
        .define("WITH_BULK_COMPRESSION", "ON")
        // Enable clipboard redirection
        .define("CHANNEL_CLIPRDR", "ON")
        .define("CHANNEL_CLIPRDR_CLIENT", "ON")
        // Enable drive/folder sharing
        .define("CHANNEL_DRIVE", "ON")
        .define("CHANNEL_DRIVE_CLIENT", "ON")
        // Enable camera/video capture redirection
        .define("CHANNEL_RDPECAM", "ON")
        .define("CHANNEL_RDPECAM_CLIENT", "ON")
        // USB device forwarding — disabled due to complex static linking
        .define("CHANNEL_URBDRC", "OFF")
        .define("CHANNEL_URBDRC_CLIENT", "OFF")
        // Enable device redirection (printers, serial, parallel, smartcard)
        .define("CHANNEL_RDPDR", "ON")
        .define("CHANNEL_RDPDR_CLIENT", "ON")
        // Enable dynamic virtual channels
        .define("CHANNEL_DRDYNVC", "ON")
        .define("CHANNEL_DRDYNVC_CLIENT", "ON")
        .build();

    // --- Link static libraries ---
    // Recursively find all directories containing .a files in the cmake output
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let mut search_dirs = std::collections::HashSet::new();

    // Standard install directories
    for subdir in ["lib", "lib64", "lib/x86_64-linux-gnu"] {
        let d = dst.join(subdir);
        if d.exists() {
            search_dirs.insert(d);
        }
    }

    // Recursively search both install dir and build dir for .a files
    fn find_static_libs(dir: &std::path::Path, dirs: &mut std::collections::HashSet<PathBuf>) {
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_dir() {
                    find_static_libs(&path, dirs);
                } else if path.extension().is_some_and(|e| e == "a") {
                    if let Some(parent) = path.parent() {
                        dirs.insert(parent.to_path_buf());
                    }
                }
            }
        }
    }
    find_static_libs(&dst, &mut search_dirs);
    find_static_libs(&out_dir.join("build"), &mut search_dirs);

    // Diagnostic: print found library directories
    for dir in &search_dirs {
        if let Ok(entries) = std::fs::read_dir(dir) {
            for e in entries.flatten() {
                let p = e.path();
                if p.extension().is_some_and(|ext| ext == "a") {
                    println!(
                        "cargo:warning=Found static lib: {}",
                        p.file_name().unwrap().to_string_lossy()
                    );
                }
            }
        }
        println!("cargo:rustc-link-search=native={}", dir.display());
    }

    // Core FreeRDP libraries
    for lib in ["freerdp3", "freerdp-client3", "winpr3", "winpr-tools3"] {
        println!("cargo:rustc-link-lib=static={lib}");
    }

    // X11 FreeRDP client library (provides RdpClientEntry for popup windows)
    println!("cargo:rustc-link-lib=static=xfreerdp-client");

    // System dependencies (Linux)
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    if target_os == "linux" {
        // SSL/TLS
        println!("cargo:rustc-link-lib=ssl");
        println!("cargo:rustc-link-lib=crypto");
        // X11
        println!("cargo:rustc-link-lib=X11");
        println!("cargo:rustc-link-lib=Xext");
        println!("cargo:rustc-link-lib=Xcursor");
        println!("cargo:rustc-link-lib=Xfixes");
        println!("cargo:rustc-link-lib=Xi");
        println!("cargo:rustc-link-lib=Xinerama");
        println!("cargo:rustc-link-lib=Xrandr");
        println!("cargo:rustc-link-lib=Xrender");
        // Multimedia (FFmpeg)
        println!("cargo:rustc-link-lib=avcodec");
        println!("cargo:rustc-link-lib=avutil");
        println!("cargo:rustc-link-lib=swresample");
        println!("cargo:rustc-link-lib=swscale");
        // System
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=z");
        println!("cargo:rustc-link-lib=zstd");
    }

    // --- Generate FFI bindings ---
    let include_dir = dst.join("include");
    let bindings = bindgen::Builder::default()
        .header(manifest_dir.join("wrapper.h").to_str().unwrap())
        .clang_arg(format!("-I{}", include_dir.join("freerdp3").display()))
        .clang_arg(format!("-I{}", include_dir.join("winpr3").display()))
        .clang_arg(format!("-I{}", include_dir.display()))
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        // FreeRDP functions
        .allowlist_function("freerdp_.*")
        .allowlist_function("RdpClientEntry")
        .allowlist_function("client_common_.*")
        // WinPR functions
        .allowlist_function("winpr_.*")
        .allowlist_function("WaitForSingleObject")
        .allowlist_function("GetExitCodeThread")
        // Types
        .allowlist_type("rdp.*")
        .allowlist_type("freerdp.*")
        .allowlist_type("RDP_CLIENT_ENTRY_POINTS.*")
        // Settings constants (FreeRDP_ServerHostname, etc.)
        .allowlist_var("FreeRDP_.*")
        .allowlist_var("FREERDP_.*")
        // WinPR constants
        .allowlist_var("WINPR_.*")
        .allowlist_var("INFINITE")
        // Generate even for complex types
        .derive_default(true)
        .generate()
        .expect("Unable to generate FreeRDP bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write FreeRDP bindings");

    // Rebuild triggers
    println!("cargo:rerun-if-changed=wrapper.h");
    println!("cargo:rerun-if-changed=patches/aad-fallback-parse.patch");
}
