# Embedded UI Assets System

## 1. Overview

rdpmanager can serve its React/Fluent UI frontend from **embedded assets** (single-binary deployment) or from **disk** (development workflow). This is the key mechanism enabling zero-dependency distribution.

- At **build time**, `build.rs` generates a Rust module (`embedded_ui.rs`) that embeds all React dist files via `include_bytes!()`.
- At **runtime**, the WebUI file handler serves files from the in-memory virtual file system (VFS) or falls back to searching for UI files on disk.
- The `src/embedded_ui.rs` module re-exports the generated code via `include!(concat!(env!("OUT_DIR"), "/embedded_ui.rs"))`.

This architecture means a release build produces a single executable containing all HTML, JS, CSS, fonts, and images — no external file dependencies required.

## 2. Build-Time Generation (`build.rs`)

The build script (`build.rs`) has two responsibilities: embedding the Windows application icon and generating the embedded UI module.

### Entry Point

```rust
fn main() {
    embed_windows_icon();   // § 4 — Windows PE icon
    generate_embedded_ui(); // This section
}
```

### Discovery (lines 34–60)

`generate_embedded_ui()` searches for a built React UI in these candidate directories (relative to `CARGO_MANIFEST_DIR`):

| Priority | Path | When it exists |
|----------|------|----------------|
| 1 | `ui-dist/` | CI builds (Rollup writes here via `ROLLUP_OUT_DIR`) |
| 2 | `src/ui-react/dist/` | Local `npm run build` output |

A candidate is valid only if it contains `index.html`. If a valid directory is found, all files are embedded. Otherwise, an empty stub is generated so the binary compiles but falls back to disk-based serving at runtime.

The function also emits `cargo:rerun-if-changed` for both candidate paths, so Cargo re-runs the build script when UI assets change.

### Generated Module (`embedded_ui.rs`) (lines 62–164)

The generated file defines the following public API:

```rust
/// A single embedded file with its URL path, raw bytes, and MIME type.
pub struct EmbeddedFile {
    pub path: &'static str,      // e.g. "/index.html", "/assets/main.js"
    pub data: &'static [u8],     // file contents via include_bytes!()
    pub mime_type: &'static str, // e.g. "text/html"
}

/// All embedded files, sorted alphabetically by path.
pub const FILES: &[EmbeddedFile] = &[
    EmbeddedFile {
        path: "/index.html",
        data: include_bytes!("/absolute/path/to/dist/index.html"),
        mime_type: "text/html",
    },
    // ... one entry per file in the dist directory
];

/// Directory paths that should redirect to their index.html.
/// e.g. "/" → "/index.html"
pub const INDEX_REDIRECTS: &[(&str, &str)] = &[
    ("/", "/index.html"),
    // ... one entry per directory containing an index.html
];

/// Look up an embedded file by URL path.
/// Normalizes the path (ensures leading `/`), tries direct match,
/// then checks index redirects for directory URLs.
pub fn lookup(path: &str) -> Option<&'static EmbeddedFile> { ... }

/// Returns true if any UI assets were embedded at build time.
pub fn has_files() -> bool { !FILES.is_empty() }
```

When no UI directory is found, `write_empty_module()` generates the same struct and function signatures but with empty data:

```rust
pub const FILES: &[EmbeddedFile] = &[];
pub const INDEX_REDIRECTS: &[(&str, &str)] = &[];
pub fn lookup(_path: &str) -> Option<&'static EmbeddedFile> { None }
pub fn has_files() -> bool { false }
```

### File Walking (lines 166–198)

`collect_files()` recursively walks the React dist directory:

1. For each file, computes a **URL path** — the path relative to the dist root, with forward slashes and a leading `/` (e.g. `/assets/index-abc123.js`).
2. For each `index.html` found, records an **index redirect** from the containing directory URL (with trailing `/`) to the full `index.html` path.
3. Backslashes are normalized to forward slashes for cross-platform consistency.

### MIME Type Detection (lines 200–216)

`guess_mime()` maps file extensions to MIME types:

| Extension | MIME Type |
|-----------|-----------|
| `.html` | `text/html` |
| `.js` | `application/javascript` |
| `.css` | `text/css` |
| `.json` | `application/json` |
| `.png` | `image/png` |
| `.jpg` / `.jpeg` | `image/jpeg` |
| `.svg` | `image/svg+xml` |
| `.ico` | `image/x-icon` |
| `.woff` | `font/woff` |
| `.woff2` | `font/woff2` |
| `.ttf` | `font/ttf` |
| `.map` | `application/json` |
| *(other)* | `application/octet-stream` |

## 3. Runtime Serving (`src/gui/main_window.rs`)

`MainWindow::initialize()` checks `embedded_ui::has_files()` to decide between VFS mode and disk mode.

### VFS Mode (Embedded Assets)

When embedded assets are available:

```rust
if embedded_ui::has_files() {
    webui_sys::webui_set_config(4 /* use_cookies */, false);
    webui_sys::webui_set_file_handler(self.window, Some(vfs_handler));
}
```

Cookies are disabled because WebUI's cookie-based authentication check would reject CSS/JS requests that arrive before `webui.js` has set the auth cookie.

The **`vfs_handler`** callback (lines 130–160):

1. Receives a filename pointer from WebUI.
2. Calls `embedded_ui::lookup(path)` to find the matching `EmbeddedFile`.
3. If found, constructs a full HTTP response:
   ```
   HTTP/1.1 200 OK\r\n
   Content-Type: {mime_type}\r\n
   Content-Length: {data_len}\r\n
   Cache-Control: no-cache\r\n
   \r\n
   {raw file bytes}
   ```
4. Allocates the response buffer via `webui_malloc()` (WebUI manages the memory).
5. Copies headers and body into the buffer and returns a pointer to it.
6. If the path is not found, returns `null` to let WebUI handle the request.

### Disk Mode (Fallback)

When `has_files()` returns false, `find_ui_path()` searches for UI files on disk:

| Priority | Path | Typical scenario |
|----------|------|-----------------|
| 1 | `ui-dist/` | CI artifact directory |
| 2 | `src/ui-react/dist/` | Local build output |
| 3 | `../src/ui-react/dist/` | Running from `build/` |
| 4 | `src/ui/` | Plain HTML fallback UI |
| 5 | `../src/ui/` | Running from `build/` |
| 6 | `{exe_dir}/ui/` | Installed alongside binary |

The first directory containing `index.html` is passed to `webui_set_root_folder()`, which tells WebUI to serve files directly from that directory.

If no UI path is found, the application logs an error and exits.

## 4. Windows Icon Embedding (`build.rs`, lines 12–26)

On Windows targets only, `embed_windows_icon()` uses the `winres` crate to embed `appicon.ico` into the executable's PE resources:

```rust
#[cfg(windows)]
{
    let mut res = winres::WindowsResource::new();
    res.set_icon("appicon.ico");
    res.compile()?;
}
```

This makes the `.exe` display the application icon in Explorer, the taskbar, and Alt-Tab.

## 5. Build Workflow

### Local Development

```
npm run build (in src/ui-react/)
  → produces src/ui-react/dist/

cargo build
  → build.rs runs:
    1. Searches for dist directory
    2. Finds src/ui-react/dist/
    3. Generates embedded_ui.rs with include_bytes!() for every file
    4. (Windows only) Embeds appicon.ico into PE resources
  → rustc compiles embedded_ui.rs (file contents become part of the binary)
  → Single executable contains all UI assets
```

### CI/CD (GitHub Actions)

The `rust-ci.yml` workflow builds the React UI before Cargo:

```yaml
- name: Build React UI
  working-directory: src/ui-react
  run: |
    npm ci --no-audit --no-fund
    npx rollup -c rollup.config.mjs
  env:
    ROLLUP_OUT_DIR: ${{ github.workspace }}/ui-dist

- name: Build release binary
  run: cargo build --release
```

Key details:
- `ROLLUP_OUT_DIR` places build output in `ui-dist/` at the workspace root (build.rs candidate #1).
- The release binary is uploaded as a per-platform artifact (`rdpmanager-linux-x86_64`, `rdpmanager-windows-x86_64`, `rdpmanager-macos-arm64`).
- Each artifact is a single self-contained executable.

## 6. Embedded vs. Disk Mode Comparison

```bash
# Development (disk mode — fast UI iteration)
cd src/ui-react && npm run dev    # React dev server with HMR
cargo build                        # No dist/ → empty stub → disk fallback at runtime

# Release (embedded mode — single binary)
cd src/ui-react && npm run build   # Build React → dist/
cargo build --release              # Embeds all dist/ files into binary
```

| Aspect | Embedded (VFS) | Disk (Fallback) |
|--------|---------------|-----------------|
| File source | Compiled into binary | Read from filesystem |
| Deployment | Single executable | Executable + UI directory |
| Startup | Instant (in-memory) | Searches candidate paths |
| UI iteration | Requires full rebuild | Edit files, refresh browser |
| Binary size | Larger (includes assets) | Smaller |
| Cookie auth | Disabled (`use_cookies = false`) | WebUI default |

## 7. Adding New Asset Types

To support a new file extension in embedded assets:

1. **Add the MIME mapping** in `build.rs`, in the `guess_mime()` function:
   ```rust
   Some("webp") => "image/webp",
   ```
2. **Rebuild** — files with the new extension in the dist directory will be automatically discovered by `collect_files()` and embedded.

No changes are needed in the runtime serving code; the VFS handler uses whatever MIME type was recorded at build time.

## Cross-References

- **[README.md](../README.md)** — Project overview and quick-start instructions.
- **[PATCHES.md](../PATCHES.md)** — Patches applied to vendored WebUI submodule (relevant to `webui_set_file_handler` API).
- **[MODERNIZATION.md](../MODERNIZATION.md)** — C++ to Rust migration notes.
- **Source files:**
  - [`build.rs`](../build.rs) — Build-time asset embedding and Windows icon.
  - [`src/embedded_ui.rs`](../src/embedded_ui.rs) — Module that re-exports generated code.
  - [`src/gui/main_window.rs`](../src/gui/main_window.rs) — Runtime VFS handler and disk fallback.
  - [`.github/workflows/rust-ci.yml`](../.github/workflows/rust-ci.yml) — CI pipeline that builds React UI then embeds into release binary.
