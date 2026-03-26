// freerdp-sys build script
//
// 1. Applies the aad-fallback-parse patch to the vendored FreeRDP source
// 2. Builds FreeRDP as a static library via CMake (platform-aware configuration)
// 3. Generates Rust FFI bindings from FreeRDP/WinPR headers via `bindgen`

use std::{collections::HashSet, env, path::PathBuf, process::Command};

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let freerdp_dir = manifest_dir.join("freerdp");
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    let is_linux = target_os == "linux";
    let is_windows = target_os == "windows";
    let is_macos = target_os == "macos";

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
    let mut config = cmake::Config::new(&freerdp_dir);

    // Common defines (all platforms)
    config
        .define("BUILD_SHARED_LIBS", "OFF")
        .define("CMAKE_INTERPROCEDURAL_OPTIMIZATION", "OFF")
        .define("CMAKE_C_VISIBILITY_PRESET", "default")
        .define("CMAKE_CXX_VISIBILITY_PRESET", "default")
        .define("WITH_CLIENT_COMMON", "ON")
        .define("WITH_CLIENT", "ON")
        .define("WITH_CLIENT_INTERFACE", "ON")
        .define("WITH_SERVER", "OFF")
        .define("WITH_SAMPLE", "OFF")
        .define("WITH_PLATFORM_SERVER", "OFF")
        .define("WITH_PROXY", "OFF")
        .define("WITH_SHADOW", "OFF")
        .define("WITH_MANPAGES", "OFF")
        .define("WITH_GSSAPI", "OFF")
        .define("WITH_KRB5", "OFF")
        .define("WITH_PKCS11", "OFF")
        .define("WITH_CLIENT_SDL", "OFF")
        .define("WITH_CLIENT_WAYLAND", "OFF")
        .define("WITH_WAYLAND", "OFF")
        .define("WITH_FUSE", "OFF")
        .define("CHANNEL_REMDESK", "OFF")
        .define("WITH_OPENH264", "OFF")
        .define("WITH_UNICODE_BUILTIN", "ON")
        // Channel configuration
        .define("CHANNEL_CLIPRDR", "ON")
        .define("CHANNEL_CLIPRDR_CLIENT", "ON")
        .define("CHANNEL_DRIVE", "ON")
        .define("CHANNEL_DRIVE_CLIENT", "ON")
        .define("CHANNEL_URBDRC", "OFF")
        .define("CHANNEL_URBDRC_CLIENT", "OFF")
        .define("CHANNEL_RDPDR", "ON")
        .define("CHANNEL_RDPDR_CLIENT", "ON")
        .define("CHANNEL_DRDYNVC", "ON")
        .define("CHANNEL_DRDYNVC_CLIENT", "ON");

    // Only enable FFmpeg when FREERDP_FFMPEG=1 is set (it requires platform-specific dev packages)
    let enable_ffmpeg = std::env::var("FREERDP_FFMPEG").unwrap_or_default() == "1";
    let ffmpeg_flag = if enable_ffmpeg { "ON" } else { "OFF" };

    // Platform-specific cmake defines
    if is_linux {
        config
            .define("WITH_X11", "ON")
            .define("WITH_AAD", "ON")
            .define("WITH_FFMPEG", ffmpeg_flag)
            .define("WITH_VIDEO_FFMPEG", ffmpeg_flag)
            .define("WITH_DSP_FFMPEG", ffmpeg_flag)
            .define("WITH_BULK_COMPRESSION", "ON")
            // V4L camera redirection (Linux only)
            .define("CHANNEL_RDPECAM", "ON")
            .define("CHANNEL_RDPECAM_CLIENT", "ON");
    } else if is_windows {
        config
            .define("WITH_X11", "OFF")
            .define("WITH_CLIENT_WINDOWS", "ON")
            .define("WITH_AAD", "ON")
            .define("WITH_FFMPEG", ffmpeg_flag)
            .define("WITH_VIDEO_FFMPEG", ffmpeg_flag)
            .define("WITH_DSP_FFMPEG", ffmpeg_flag)
            .define("WITH_BULK_COMPRESSION", "ON")
            .define("WITH_NATIVE_SSPI", "ON")
            .define("CHANNEL_RDPECAM", "OFF")
            .define("CHANNEL_RDPECAM_CLIENT", "OFF");
    } else if is_macos {
        config
            .define("WITH_X11", "OFF")
            .define("WITH_CLIENT_MAC", "ON")
            .define("WITH_AAD", "ON")
            .define("WITH_FFMPEG", ffmpeg_flag)
            .define("WITH_VIDEO_FFMPEG", ffmpeg_flag)
            .define("WITH_DSP_FFMPEG", ffmpeg_flag)
            .define("WITH_BULK_COMPRESSION", "ON")
            .define("CHANNEL_RDPECAM", "OFF")
            .define("CHANNEL_RDPECAM_CLIENT", "OFF")
            // Map C++ nullptr → NULL for ObjC files compiled as plain C (Keyboard.m)
            .define("CMAKE_OBJC_FLAGS", "-Dnullptr=NULL");
    }

    // OpenSSL location (CI sets OPENSSL_ROOT_DIR; fallback to brew on macOS)
    if let Ok(ssl_dir) = env::var("OPENSSL_ROOT_DIR") {
        config.define("OPENSSL_ROOT_DIR", &ssl_dir);
    } else if is_macos {
        if let Some(dir) = brew_prefix("openssl@3") {
            config.define("OPENSSL_ROOT_DIR", &dir);
        }
    }

    // FFmpeg location (CI sets FFMPEG_DIR; fallback to brew/vcpkg)
    if enable_ffmpeg {
        if let Ok(ffmpeg_dir) = env::var("FFMPEG_DIR") {
            config.define("FFMPEG_DIR", &ffmpeg_dir);
        } else if is_macos {
            if let Some(dir) = brew_prefix("ffmpeg") {
                config.define("CMAKE_PREFIX_PATH", &dir);
            }
        }
    }

    // cJSON / jansson for WinPR JSON backend (needed for AAD auth)
    if is_macos {
        if let Some(dir) = brew_prefix("cjson") {
            config.define("cJSON_DIR", &format!("{}/lib/cmake/cJSON", dir));
        } else if let Some(dir) = brew_prefix("jansson") {
            config.define("JANSSON_DIR", &format!("{}/lib/cmake/jansson", dir));
        }
    }

    // Windows: use vcpkg toolchain if available (provides zlib, OpenSSL, FFmpeg, cJSON)
    if is_windows {
        if let Ok(toolchain) = env::var("CMAKE_TOOLCHAIN_FILE") {
            config.define("CMAKE_TOOLCHAIN_FILE", &toolchain);
        }
        if let Ok(triplet) = env::var("VCPKG_TARGET_TRIPLET") {
            config.define("VCPKG_TARGET_TRIPLET", &triplet);
        }
    }

    let dst = config.build();

    // --- Link static libraries ---
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let lib_ext = if is_windows { "lib" } else { "a" };
    let mut search_dirs = HashSet::new();

    // Standard install directories
    for subdir in ["lib", "lib64", "lib/x86_64-linux-gnu"] {
        let d = dst.join(subdir);
        if d.exists() {
            search_dirs.insert(d);
        }
    }

    // Recursively search cmake output for static libraries
    find_static_libs(&dst, lib_ext, &mut search_dirs);
    find_static_libs(&out_dir.join("build"), lib_ext, &mut search_dirs);

    // Print found library paths (useful for CI debugging)
    for dir in &search_dirs {
        if let Ok(entries) = std::fs::read_dir(dir) {
            for e in entries.flatten() {
                let p = e.path();
                if p.extension().is_some_and(|ext| ext == lib_ext) {
                    println!(
                        "cargo:warning=Found static lib: {}",
                        p.file_name().unwrap().to_string_lossy()
                    );
                }
            }
        }
        println!("cargo:rustc-link-search=native={}", dir.display());
    }

    // Core FreeRDP libraries (all platforms, version suffix "3")
    // Order: most dependent first (platform-client → freerdp-client3 → freerdp3 → winpr3)
    if is_linux {
        println!("cargo:rustc-link-lib=static=xfreerdp-client3");
    } else if is_windows {
        println!("cargo:rustc-link-lib=static=wfreerdp-client3");
    } else if is_macos {
        println!("cargo:rustc-link-lib=static=MacFreeRDP-library");
    }
    for lib in ["freerdp-client3", "freerdp3", "winpr-tools3", "winpr3"] {
        println!("cargo:rustc-link-lib=static={lib}");
    }
    // Repeat for circular dependencies between FreeRDP static archives
    for lib in ["freerdp-client3", "freerdp3", "winpr3"] {
        println!("cargo:rustc-link-lib=static={lib}");
    }

    // --- System dependencies ---
    if is_linux {
        println!("cargo:rustc-link-lib=ssl");
        println!("cargo:rustc-link-lib=crypto");
        for lib in [
            "X11", "Xext", "Xcursor", "Xfixes", "Xi", "Xinerama", "Xrandr", "Xrender",
        ] {
            println!("cargo:rustc-link-lib={lib}");
        }
        if enable_ffmpeg {
            for lib in ["avcodec", "avutil", "swresample", "swscale"] {
                println!("cargo:rustc-link-lib={lib}");
            }
        }
        // WinPR JSON backend (cJSON on Linux)
        println!("cargo:rustc-link-lib=cjson");
        // ALSA (sound channel)
        println!("cargo:rustc-link-lib=asound");
        // CUPS (printer redirection)
        println!("cargo:rustc-link-lib=cups");
        // libusb (USB device redirection)
        println!("cargo:rustc-link-lib=usb-1.0");
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=z");
        println!("cargo:rustc-link-lib=zstd");
    } else if is_windows {
        // OpenSSL and zlib from vcpkg or standalone install
        if let Ok(ssl_dir) = env::var("OPENSSL_ROOT_DIR") {
            let lib_dir = PathBuf::from(&ssl_dir).join("lib");
            if lib_dir.exists() {
                println!("cargo:rustc-link-search=native={}", lib_dir.display());
            }
            // Some installs put libs in lib/VC/x64/MD or similar
            let vc_dir = lib_dir.join("VC").join("x64").join("MD");
            if vc_dir.exists() {
                println!("cargo:rustc-link-search=native={}", vc_dir.display());
            }
        }
        println!("cargo:rustc-link-lib=libssl");
        println!("cargo:rustc-link-lib=libcrypto");
        // zlib (from vcpkg: zlib.lib, or system: zlib.lib)
        println!("cargo:rustc-link-lib=zlib");
        // FFmpeg libraries (from vcpkg)
        if enable_ffmpeg {
            if let Ok(ffmpeg_dir) = env::var("FFMPEG_DIR") {
                let lib_dir = PathBuf::from(&ffmpeg_dir).join("lib");
                if lib_dir.exists() {
                    println!("cargo:rustc-link-search=native={}", lib_dir.display());
                }
            }
            for lib in ["avcodec", "avutil", "swresample", "swscale"] {
                println!("cargo:rustc-link-lib={lib}");
            }
        }
        // WinPR JSON backend (cJSON from vcpkg, needed for AAD auth)
        println!("cargo:rustc-link-lib=cjson");
        // Windows system libraries
        for lib in [
            "ws2_32", "rpcrt4", "crypt32", "ncrypt", "bcrypt", "secur32", "advapi32", "user32",
            "gdi32", "shell32", "ole32", "ntdll", "iphlpapi", "winmm", "shlwapi", "dbghelp",
            "msimg32", "credui",
        ] {
            println!("cargo:rustc-link-lib={lib}");
        }
    } else if is_macos {
        // OpenSSL from Homebrew
        let ssl_dir = env::var("OPENSSL_ROOT_DIR")
            .ok()
            .or_else(|| brew_prefix("openssl@3"))
            .unwrap_or_default();
        if !ssl_dir.is_empty() {
            println!("cargo:rustc-link-search=native={}/lib", ssl_dir);
        }
        // FFmpeg from Homebrew
        if enable_ffmpeg {
            let ffmpeg_dir = env::var("FFMPEG_DIR")
                .ok()
                .or_else(|| brew_prefix("ffmpeg"))
                .unwrap_or_default();
            if !ffmpeg_dir.is_empty() {
                println!("cargo:rustc-link-search=native={}/lib", ffmpeg_dir);
            }
        }
        // cJSON from Homebrew (WinPR JSON backend, needed for AAD auth)
        let cjson_dir = brew_prefix("cjson").unwrap_or_default();
        if !cjson_dir.is_empty() {
            println!("cargo:rustc-link-search=native={}/lib", cjson_dir);
        }
        println!("cargo:rustc-link-lib=ssl");
        println!("cargo:rustc-link-lib=crypto");
        println!("cargo:rustc-link-lib=z");
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=iconv");
        // FFmpeg libraries
        if enable_ffmpeg {
            for lib in ["avcodec", "avutil", "swresample", "swscale"] {
                println!("cargo:rustc-link-lib={lib}");
            }
        }
        // cJSON (WinPR JSON backend for AAD)
        println!("cargo:rustc-link-lib=cjson");
        for fw in [
            "CoreFoundation",
            "Security",
            "IOKit",
            "Cocoa",
            "CoreGraphics",
            "AppKit",
        ] {
            println!("cargo:rustc-link-lib=framework={fw}");
        }
    }

    // --- Generate FFI bindings ---
    let include_dir = dst.join("include");
    let mut builder = bindgen::Builder::default()
        .header(manifest_dir.join("wrapper.h").to_str().unwrap())
        .clang_arg(format!("-I{}", include_dir.join("freerdp3").display()))
        .clang_arg(format!("-I{}", include_dir.join("winpr3").display()))
        .clang_arg(format!("-I{}", include_dir.display()))
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .allowlist_function("freerdp_.*")
        .allowlist_function("RdpClientEntry")
        .allowlist_function("client_common_.*")
        .allowlist_function("winpr_.*")
        .allowlist_function("WaitForSingleObject")
        .allowlist_function("GetExitCodeThread")
        .allowlist_type("rdp.*")
        .allowlist_type("freerdp.*")
        .allowlist_type("RDP_CLIENT_ENTRY_POINTS.*")
        .allowlist_type("FreeRDP_Settings_Keys.*")
        .allowlist_type("MONITOR_DEF")
        .allowlist_var("DISPLAY_CONTROL_.*")
        .allowlist_var("FreeRDP_.*")
        .allowlist_var("FREERDP_.*")
        .allowlist_var("WINPR_.*")
        .allowlist_var("INFINITE")
        .prepend_enum_name(false)
        .derive_default(true);

    // Help clang find OpenSSL headers on macOS (not in default search path)
    if is_macos {
        let ssl_dir = env::var("OPENSSL_ROOT_DIR")
            .ok()
            .or_else(|| brew_prefix("openssl@3"))
            .unwrap_or_default();
        if !ssl_dir.is_empty() {
            builder = builder.clang_arg(format!("-I{}/include", ssl_dir));
        }
    }

    let bindings = builder
        .generate()
        .expect("Unable to generate FreeRDP bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write FreeRDP bindings");

    println!("cargo:rerun-if-changed=wrapper.h");
    println!("cargo:rerun-if-changed=patches/aad-fallback-parse.patch");
}

/// Recursively find directories containing static libraries
fn find_static_libs(dir: &std::path::Path, ext: &str, dirs: &mut HashSet<PathBuf>) {
    if let Ok(entries) = std::fs::read_dir(dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                find_static_libs(&path, ext, dirs);
            } else if path.extension().is_some_and(|e| e == ext) {
                if let Some(parent) = path.parent() {
                    dirs.insert(parent.to_path_buf());
                }
            }
        }
    }
}

/// Get a Homebrew prefix for a package (macOS only)
fn brew_prefix(package: &str) -> Option<String> {
    Command::new("brew")
        .args(["--prefix", package])
        .output()
        .ok()
        .and_then(|o| {
            let s = String::from_utf8_lossy(&o.stdout).trim().to_string();
            if s.is_empty() {
                None
            } else {
                Some(s)
            }
        })
}
