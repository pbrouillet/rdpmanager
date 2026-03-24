# Debugging Guide

Comprehensive guide for debugging rdpmanager — the Rust/React RDP connection manager.

> See also: [Architecture](architecture.md) · [WebUI Bindings](webui-bindings.md) · [SQLite & Persistence](sqlite.md) · [Embedded Assets](embedded-assets.md) · [Session Tabs](session-tabs.md)

---

## 1. Logging

rdpmanager uses the [`env_logger`](https://docs.rs/env_logger) + [`log`](https://docs.rs/log) crate combination.

Logging is initialized early in `main.rs`:

```rust
env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"))
    .format_timestamp_millis()
    .init();
```

- **Default level:** `info`
- **Timestamp format:** millisecond precision
- **Log macros used throughout:** `log::info!`, `log::warn!`, `log::error!`, `log::debug!`

### RUST_LOG Environment Variable

The `RUST_LOG` environment variable controls log output at runtime without recompilation.

```bash
# Default (info level)
./rdpmanager

# Debug everything
RUST_LOG=debug ./rdpmanager

# Debug a specific module
RUST_LOG=rdpmanager::config_manager=debug ./rdpmanager

# Multiple modules at different levels
RUST_LOG=rdpmanager::js_handlers=debug,rdpmanager::rdp_launcher=trace ./rdpmanager

# Quiet mode (errors only)
RUST_LOG=error ./rdpmanager

# Windows PowerShell
$env:RUST_LOG="debug"; .\rdpmanager.exe

# Windows CMD
set RUST_LOG=debug && rdpmanager.exe
```

### Key Log Points

| Module | Level | What it logs |
|--------|-------|-------------|
| `main.rs` | info | App version, startup, window init, shutdown |
| `gui/main_window.rs` | info/warn/error | UI mode selection (embedded VFS vs disk fallback), window creation, WebView errors |
| `js_handlers.rs` | info/warn | Binding registration (35 bindings), file picker dialogs, database operations |
| `config_manager.rs` | info/error | Database open/close/clone, SQL errors, backup failures, JSON parse errors |
| `rdp_launcher.rs` | info/warn | Session launch (`hostname:port`), FreeRDP thread lifecycle, state transitions |
| `session_manager.rs` | info/warn | Tab switches, session connect/disconnect |
| `window_embedding.rs` | info | Phase 1 embedding stubs (all no-op messages) |

### Reading Log Output

Log lines follow this format:

```
[2025-01-15T10:23:45.123Z INFO  rdpmanager::config_manager] Opened database: /home/user/.config/webui-rdp-client/connections.db
[2025-01-15T10:23:45.456Z INFO  rdpmanager::gui::main_window] Using embedded UI (VFS mode — assets compiled into binary)
[2025-01-15T10:23:45.789Z INFO  rdpmanager::js_handlers] Registered 35 JS bindings
```

---

## 2. Build Debugging

### FreeRDP Build Issues

The `crates/freerdp-sys/build.rs` has extensive diagnostics. It outputs `cargo:warning=` messages during compilation for:

- Patch application status
- Static library discovery
- Library search paths and link declarations

```bash
# Verbose cargo build to see build script output
cargo build -vv 2>&1 | grep "freerdp-sys"

# Check what symbols are in the static library (Linux)
nm -g target/debug/build/freerdp-sys-*/out/lib/libfreerdp3.a | grep freerdp_settings

# List discovered static libraries
cargo build -vv 2>&1 | grep "Found static lib"
```

### Common Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `undefined symbol: freerdp_*` | LTO bitcode objects | Ensure `CMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF` in build.rs (already set) |
| `FreeRDP_ServerHostname not found` | Enum name prepending | Ensure `.prepend_enum_name(false)` in bindgen config (already set) |
| `expected i32, found usize` | Wrong cast in settings macro | Use `as _` instead of `as usize` |
| `cJSON_* undefined` | Missing system dep | Install cJSON dev package; add `cjson` to system deps |
| `snd_pcm_* undefined` | Missing ALSA | Install ALSA dev package; add `asound` to system deps |
| `cups* undefined` | Missing CUPS | Install CUPS dev package; add `cups` to system deps |
| `libusb_* undefined` | Missing libusb | Install libusb dev package; add `usb-1.0` to system deps |
| Circular link errors | FreeRDP inter-library deps | build.rs already links core libs twice to resolve cycles |

### Embedded UI Issues

```bash
# Check if UI was embedded during build
cargo build 2>&1 | grep "embedded_ui"
# Should see: "Found UI directory at ..."

# If "No UI directory found" — build React UI first:
cd src/ui-react && npm run build && cd ../..
cargo build
```

The embedded UI build compiles React assets into the binary. If the assets are missing, the app falls back to searching for UI files on disk in this order:

1. `ui-dist/`
2. `src/ui-react/dist/`
3. `../src/ui-react/dist/`
4. `src/ui/`
5. `../src/ui/`
6. `{exe_dir}/ui/`

See [Embedded Assets](embedded-assets.md) for the full embedding pipeline.

---

## 3. Runtime Debugging

### App Starts and Exits Immediately

- Check for the **"Window opened"** or **"Using embedded UI"** log messages
- Ensure `webui_wait()` is called (with timeout 0 for infinite wait)
- Check the event handler — `WEBUI_EVENT_DISCONNECTED` triggers `webui_exit()`
- If no UI files are found, look for: `"No UI files found! Build the React UI first..."`

### JS Bindings Not Working

- Check browser DevTools console (F12 if the WebView supports it)
- Ensure the binding is registered in `bind_all()` (`js_handlers.rs` — 35 bindings)
- Ensure the TypeScript declaration exists in `webui.d.ts`
- Look for `ReferenceError: functionName is not defined` → binding not registered
- Check log for `"Registered 35 JS bindings"` — if the count is wrong, a binding is missing

See [WebUI Bindings](webui-bindings.md) for the full binding list and how to add new ones.

### Database Issues

The default database location is platform-dependent:

| Platform | Path |
|----------|------|
| Linux | `~/.config/webui-rdp-client/connections.db` |
| macOS | `~/Library/Application Support/webui-rdp-client/connections.db` |
| Windows | `%APPDATA%\webui-rdp-client\connections.db` |
| Fallback | `.webui-rdp-client/connections.db` (current directory) |

```bash
# Inspect database schema (Linux/macOS)
sqlite3 ~/.config/webui-rdp-client/connections.db ".schema"

# List all connections
sqlite3 ~/.config/webui-rdp-client/connections.db "SELECT * FROM connections"

# Check folder hierarchy
sqlite3 ~/.config/webui-rdp-client/connections.db "SELECT * FROM folders"
```

```powershell
# Windows PowerShell
sqlite3 "$env:APPDATA\webui-rdp-client\connections.db" ".schema"
```

See [SQLite & Persistence](sqlite.md) for the full database schema and inheritance model.

### RDP Session Issues

- Check FreeRDP static libs were built:
  ```bash
  ls target/debug/build/freerdp-sys-*/out/lib/
  ```
- Enable detailed session logging:
  ```bash
  RUST_LOG=rdpmanager::rdp_launcher=debug ./rdpmanager
  ```
- Look for session launch log: `"Launching RDP session {id} to {host}:{port}"`
- Look for thread exit log: `"FreeRDP thread exited"`
- **Common:** "RDP sessions require FreeRDP platform client" on non-Linux platforms (xfreerdp-client is Linux-only)

---

## 4. Platform-Specific Debugging

### Linux

```bash
# GDB — full debug session
RUST_LOG=debug gdb --args ./target/debug/rdpmanager
# In GDB: run, then bt on crash

# Valgrind — memory leak detection
valgrind --leak-check=full ./target/debug/rdpmanager

# strace — trace system/network calls
strace -f -e trace=network ./target/debug/rdpmanager

# Check linked libraries
ldd ./target/debug/rdpmanager
```

### Windows

```powershell
# Visual Studio debugger
# Open rdpmanager.sln or folder in VS, set rdpmanager as startup project, press F5

# WinDbg
windbg target\debug\rdpmanager.exe

# Event Viewer for crash dumps
eventvwr.msc

# Check DLL dependencies
dumpbin /dependents target\debug\rdpmanager.exe
```

### macOS

```bash
# lldb — full debug session
RUST_LOG=debug lldb ./target/debug/rdpmanager
# In lldb: run, then bt on crash

# Instruments — CPU profiling
instruments -t "Time Profiler" ./target/debug/rdpmanager

# Check linked libraries
otool -L ./target/debug/rdpmanager
```

---

## 5. VSCode Integration

Add a Rust debug configuration to `.vscode/launch.json`:

```json
// .vscode/launch.json — add to "configurations" array
{
  "type": "lldb",
  "request": "launch",
  "name": "Debug rdpmanager",
  "cargo": {
    "args": ["build", "--bin=rdpmanager"],
    "filter": { "name": "rdpmanager", "kind": "bin" }
  },
  "env": { "RUST_LOG": "debug" },
  "cwd": "${workspaceFolder}"
}
```

> **Note:** The existing `.vscode/launch.json` contains legacy C++ configurations
> targeting `build/webui-rdp-client`. The configuration above is for the current
> Rust binary. Both can coexist in the same `configurations` array.

### Recommended Extensions

- [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb) — Rust/C++ debugging with LLDB
- [rust-analyzer](https://marketplace.visualstudio.com/items?itemName=rust-lang.rust-analyzer) — Language server with inline errors
- [SQLite Viewer](https://marketplace.visualstudio.com/items?itemName=qwtel.sqlite-viewer) — Inspect the connections database

---

## 6. React Frontend Debugging

The React UI lives in `src/ui-react/` and uses Rollup for bundling.

```bash
# Watch mode — rebuilds on file changes
cd src/ui-react && npm run dev

# Full production build
cd src/ui-react && npm run build
```

- **Dev workflow:** Run `npm run dev` (Rollup watch), then rebuild the Rust binary to pick up changes — or rely on disk fallback if not using embedded mode
- **Disk fallback:** When no embedded UI is compiled in, the Rust backend searches for `dist/` on disk (see [search order](#embedded-ui-issues) above)
- Use **browser DevTools** (F12) for React component inspection, network tab, and console
- Check the DevTools **console** for WebUI binding call errors
- Check the **network tab** for failed resource loads (missing assets = embedding issue)

### TypeScript Errors

```bash
# Type-check without building
cd src/ui-react && npx tsc --noEmit

# Full build (type-check + rollup)
cd src/ui-react && npm run build
```

---

## Quick Reference

```bash
# "It won't build"
cargo build -vv 2>&1 | tail -50

# "It crashes on startup"
RUST_LOG=debug ./target/debug/rdpmanager

# "JS bindings aren't working"
RUST_LOG=rdpmanager::js_handlers=debug ./target/debug/rdpmanager
# + check browser DevTools console

# "Database is corrupt"
sqlite3 ~/.config/webui-rdp-client/connections.db "PRAGMA integrity_check"

# "RDP won't connect"
RUST_LOG=rdpmanager::rdp_launcher=debug,rdpmanager::session_manager=debug ./target/debug/rdpmanager

# "UI is blank"
cargo build 2>&1 | grep -i "ui\|embedded\|vfs"
# Then check: cd src/ui-react && npm run build
```
