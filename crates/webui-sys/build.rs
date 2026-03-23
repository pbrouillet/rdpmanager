// webui-sys build script
//
// 1. Applies the navigate-passthrough patch to the vendored WebUI source
// 2. Compiles WebUI + embedded civetweb as a static library via `cc`
// 3. Generates Rust FFI bindings from webui.h via `bindgen`

use std::{env, path::PathBuf, process::Command};

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let webui_dir = manifest_dir.join("webui");

    // --- Apply local patch (navigate-passthrough for OAuth flows) ---
    let patch = manifest_dir
        .join("patches")
        .join("navigate-passthrough.patch");
    if patch.exists() && webui_dir.join(".git").exists() {
        let check = Command::new("git")
            .args(["apply", "--check"])
            .arg(&patch)
            .current_dir(&webui_dir)
            .output();

        if let Ok(output) = check {
            if output.status.success() {
                let _ = Command::new("git")
                    .args(["apply", "--3way"])
                    .arg(&patch)
                    .current_dir(&webui_dir)
                    .status();
                println!("cargo:warning=Applied navigate-passthrough.patch to WebUI");
            }
            // If check fails, patch is likely already applied — that's fine
        }
    }

    // --- Compile WebUI + civetweb as a static library ---
    let mut build = cc::Build::new();

    build
        .define("WEBUI_STATIC", None)
        .define("NO_SSL", None)
        .define("NDEBUG", None)
        .define("NO_CACHING", None)
        .define("NO_CGI", None)
        .define("USE_WEBSOCKET", None)
        .include(webui_dir.join("include"))
        .include(webui_dir.join("src").join("civetweb"))
        .include(webui_dir.join("src").join("webview"))
        .file(webui_dir.join("src").join("webui.c"))
        .file(webui_dir.join("src").join("civetweb").join("civetweb.c"));

    // Platform-specific defines
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    match target_os.as_str() {
        "linux" => {
            build.define("_GNU_SOURCE", None);
            build.define("WEBUI_LOG", None);
        }
        "windows" => {
            build.define("WIN32", None);
        }
        _ => {}
    }

    build.compile("webui");

    // Platform-specific link libraries
    match target_os.as_str() {
        "windows" => {
            println!("cargo:rustc-link-lib=user32");
            println!("cargo:rustc-link-lib=shell32");
            println!("cargo:rustc-link-lib=ole32");
            println!("cargo:rustc-link-lib=ws2_32");
        }
        "linux" => {
            println!("cargo:rustc-link-lib=pthread");
        }
        "macos" => {
            println!("cargo:rustc-link-lib=pthread");
            println!("cargo:rustc-link-lib=framework=CoreGraphics");
        }
        _ => {}
    }

    // --- Generate FFI bindings ---
    let mut builder = bindgen::Builder::default()
        .header(manifest_dir.join("wrapper.h").to_str().unwrap())
        .clang_arg(format!("-I{}", webui_dir.join("include").display()))
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .allowlist_function("webui_.*")
        .allowlist_type("webui_.*")
        .allowlist_var("WEBUI_.*")
        // Enums used by the project
        .allowlist_type("webui_browser")
        .allowlist_type("webui_event")
        .allowlist_type("webui_config");

    // macOS: bindgen's clang needs the SDK sysroot to find framework headers
    if target_os == "macos" {
        if let Ok(output) = std::process::Command::new("xcrun")
            .args(["--sdk", "macosx", "--show-sdk-path"])
            .output()
        {
            let sdk = String::from_utf8_lossy(&output.stdout).trim().to_string();
            if !sdk.is_empty() {
                builder = builder.clang_arg(format!("-isysroot{}", sdk));
            }
        }
    }

    let bindings = builder
        .generate()
        .expect("Unable to generate WebUI bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write WebUI bindings");

    // Tell cargo to re-run if source changes
    println!("cargo:rerun-if-changed=wrapper.h");
    println!(
        "cargo:rerun-if-changed={}",
        webui_dir.join("src").join("webui.c").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        webui_dir.join("include").join("webui.h").display()
    );
}
