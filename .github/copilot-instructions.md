# Copilot Instructions

## Build

This project uses Meson + Ninja with vendored submodules (FreeRDP via CMake, WebUI via wrap). The build has multiple stages — don't skip any.

### First-time setup

```bash
source ./setup-deps.sh   # install dev packages (handles no-sudo fallback)
./init.sh                 # submodules → patches → npm install → meson setup → ninja build
```

`init.sh` is idempotent and performs a clean build: it reinitializes submodules, applies patches from `patches/`, installs React UI npm dependencies, runs `meson setup`, and builds with `ninja -C build`.

### Rebuilding after code changes

```bash
ninja -C build            # incremental C++ build
```

If you changed React UI code in `src/ui-react/`:

```bash
ninja -C build            # the react-ui custom_target rebuilds automatically
```

### Embedded UI build (single-binary deployment)

```bash
meson setup build-embedded --buildtype=release -Dembed_ui=true
ninja -C build-embedded
```

This runs `scripts/generate_vfs.py` to embed all React assets as a C++ header (`embedded_ui.hpp`), producing a self-contained executable.

### Running

```bash
./build/webui-rdp-client
```

## Architecture

C++23 backend with a React 19 / Fluent UI / TypeScript frontend, connected via WebUI's JavaScript binding layer.

```
React/Fluent UI (src/ui-react/)  ←→  JS bindings (js_handlers.cpp)  ←→  C++ backend
                                                                          ├── RDPLauncher (FreeRDP sessions)
                                                                          ├── ConfigManager (SQLite persistence)
                                                                          ├── AADAuthHandler (Azure AD OAuth popup)
                                                                          ├── FeedDiscoveryManager (WVD feed import)
                                                                          └── DialogManager (cert/auth dialog sync)
```

- **Threading**: FreeRDP callbacks run on worker threads. Dialog and auth flows use `std::mutex` + `std::condition_variable` to synchronize with the main thread. Be careful with thread safety.
- **Persistence**: SQLite database with tables for connections, folders (hierarchical), folder_settings (sparse parameter inheritance), cached_tokens, and feed_accounts.
- **UI fallback**: If npm/node aren't available, meson falls back to the plain HTML UI in `src/ui/`.

## Key conventions

- RAII wrappers in headers: `json_utils.hpp` (Jansson), `sqlite_helpers.hpp` (SQLite), `utils.hpp` (general).
- Connection parameters use folder-level inheritance — child folders/connections inherit settings from parent folders up to root.
- Two local patches are applied to vendored submodules (`patches/freerdp/`, `patches/webui/`). See `PATCHES.md` for details. Changes to FreeRDP or WebUI behavior should be made as patch files, not direct submodule edits.
