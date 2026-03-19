# WebUI RDP Client

A native C++23 Remote Desktop client featuring a modern React-based UI powered by [WebUI](https://github.com/webui-dev/webui) and [FreeRDP](https://github.com/FreeRDP/FreeRDP).

![Main Windows screenshot](./docs/main-window.png)

## Features

- 🖥️ Modern React 19 / Fluent UI dark-themed web interface
- 🔐 Save and manage RDP connection profiles with SQLite persistence
- 📁 Hierarchical folder organization with cascading settings inheritance
- 🚀 Spawn FreeRDP sessions with a single click
- 🗄️ Multi-database support (create, open, clone databases)
- 🔑 Azure AD / Entra ID authentication (OAuth2 code flow with popup)
- 🌐 WVD/AVD feed discovery and import
- 📋 Clipboard redirection support
- 🔊 Audio redirection support (PulseAudio)
- 🛡️ Certificate handling (TOFU model with interactive dialogs)
- 📄 RDP file import/parsing
- ⚡ Advanced RDP options (USB, floatbar, dynamic resolution, gateway, compression, auto-reconnect)
- 📦 Embedded UI mode for single-binary deployment

## Architecture

```
React/Fluent UI (src/ui-react/)  ←→  JS bindings (js_handlers.cpp)  ←→  C++ backend
                                                                          ├── RDPLauncher (FreeRDP sessions)
                                                                          ├── ConfigManager (SQLite persistence)
                                                                          ├── AADAuthHandler (Azure AD OAuth popup)
                                                                          ├── FeedDiscoveryManager (WVD feed import)
                                                                          └── DialogManager (cert/auth dialog sync)
```

- **Frontend**: React 19 + TypeScript + Vite, styled with Microsoft Fluent UI
- **Backend**: C++23 with RAII wrappers, connected to the frontend via WebUI's JavaScript binding layer
- **Threading**: FreeRDP callbacks run on worker threads; dialog and auth flows use `std::mutex` + `std::condition_variable` to synchronize with the main thread
- **Persistence**: SQLite database (`connections.db`) with tables for connections, folders, folder_settings, cached_tokens, and feed_accounts
- **Patches**: Two local patches are applied to vendored submodules (see [PATCHES.md](PATCHES.md))

## Prerequisites

### Linux (Ubuntu/Debian)

```bash
# Build tools
sudo apt update
sudo apt install -y build-essential meson ninja-build cmake pkg-config git

# Runtime TLS backend for embedded WebView (AAD auth popup)
sudo apt install -y glib-networking ca-certificates

# Node.js (for React UI build)
sudo apt install -y nodejs npm

# FreeRDP dependencies
sudo apt install -y libssl-dev libx11-dev libxext-dev libxinerama-dev \
    libxcursor-dev libxkbfile-dev libxv-dev libxi-dev libxdamage-dev \
    libxrandr-dev libxrender-dev libxfixes-dev libasound2-dev libpulse-dev \
    libcups2-dev libusb-1.0-0-dev libudev-dev libdbus-glib-1-dev \
    libpcsclite-dev libsystemd-dev libavcodec-dev libavutil-dev \
    libswscale-dev libfuse3-dev libcairo2-dev libfaac-dev libfaad-dev \
    libgsm1-dev libpng-dev libjpeg-dev libswresample-dev

# FreeRDP client (for testing/fallback)
sudo apt install -y freerdp2-x11
```

### Arch Linux

```bash
sudo pacman -S base-devel meson ninja cmake git openssl \
    freerdp libx11 libxext libxinerama libxcursor libxkbfile \
    alsa-lib pulseaudio libcups libusb
```

### macOS

```bash
brew install meson ninja cmake openssl freerdp
```

### Windows (MSYS2/MinGW)

```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-meson \
    mingw-w64-x86_64-ninja mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-freerdp git
```

## Building

### First-time setup

```bash
git clone <your-repo-url> webui-rdp-client
cd webui-rdp-client

# Install dev packages (handles no-sudo fallback)
source ./setup-deps.sh

# Submodules → patches → npm install → meson setup → ninja build
./init.sh
```

`init.sh` is idempotent and performs a clean build: it reinitializes submodules, applies patches from `patches/`, installs React UI npm dependencies, runs `meson setup`, and builds with `ninja -C build`.

### Rebuilding after code changes

```bash
ninja -C build            # incremental C++ build (also rebuilds React UI if changed)
```

### Embedded UI build (single-binary deployment)

```bash
meson setup build-embedded --buildtype=release -Dembed_ui=true
ninja -C build-embedded
```

This embeds all React assets as a C++ header (`embedded_ui.hpp`), producing a self-contained executable.

### Run

```bash
./build/webui-rdp-client
```

### Build options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `use_system_freerdp` | boolean | false | Use system-installed FreeRDP instead of submodule |
| `use_system_webui` | boolean | false | Use system-installed WebUI instead of wrap |
| `embed_ui` | boolean | false | Embed React assets into the binary |
| `default_rdp_port` | integer | 3389 | Default RDP port for connections |
| `enable_clipboard` | boolean | true | Enable clipboard redirection |
| `enable_audio` | boolean | true | Enable audio redirection |

## VSCode Development

This project includes full VSCode integration:

### Recommended Extensions

- **C/C++** (`ms-vscode.cpptools`) - IntelliSense, debugging
- **Meson** (`mesonbuild.mesonbuild`) - Build system integration
- **CodeLLDB** (`vadimcn.vscode-lldb`) - Alternative debugger

### Build Tasks

- `Ctrl+Shift+B` - Build (Debug)
- `Meson: Setup (Debug)` - Configure debug build
- `Meson: Setup (Release)` - Configure release build
- `Full Build: Setup + Build (Debug)` - Clean build

### Debugging

1. Press `F5` to start debugging
2. Select a debug configuration:
   - **Debug: WebUI RDP Client (GDB)** - Standard GDB debugging
   - **Debug: WebUI RDP Client (LLDB)** - LLDB debugging
   - **Debug: WebUI RDP Client (CodeLLDB)** - CodeLLDB extension

## Project Structure

```
webui-rdp-client/
├── .vscode/                # VSCode configuration
│   ├── tasks.json          # Build tasks
│   ├── launch.json         # Debug configurations
│   ├── c_cpp_properties.json
│   ├── settings.json
│   └── extensions.json
├── src/
│   ├── main.cpp            # Application entry point
│   ├── js_handlers.cpp/hpp # WebUI ↔ C++ JS binding layer
│   ├── rdp_launcher.cpp/hpp    # FreeRDP session management
│   ├── config_manager.cpp/hpp  # SQLite persistence & multi-database
│   ├── dialog_manager.cpp/hpp  # Thread-safe dialog synchronization
│   ├── feed_discovery.cpp/hpp  # WVD/AVD feed discovery & import
│   ├── rdp_file_parser.cpp/hpp # RDP file import/parsing
│   ├── connection_types.hpp    # Connection & folder settings types
│   ├── json_utils.hpp         # RAII Jansson wrapper
│   ├── sqlite_helpers.hpp     # RAII SQLite wrapper
│   ├── logger.hpp             # Logging utilities
│   ├── path_utils.hpp         # Path utilities
│   ├── utils.hpp              # General RAII helpers
│   ├── gui/
│   │   ├── aad_auth_handler.cpp/hpp  # Azure AD OAuth popup
│   │   └── main_window.cpp/hpp       # Main window management
│   ├── ui/                 # Legacy plain HTML UI (fallback)
│   └── ui-react/           # React 19 / Fluent UI / TypeScript frontend
│       ├── package.json
│       ├── tsconfig.json
│       ├── vite.config.ts
│       ├── build-ui.sh
│       └── src/
│           ├── main.tsx, App.tsx
│           ├── api.ts, types.ts, theme.ts
│           └── components/
│               ├── AppHeader.tsx
│               ├── Toolbar.tsx
│               ├── FolderTree.tsx
│               ├── ConnectionGrid.tsx
│               ├── ConnectionTable.tsx
│               ├── ConnectionEditorDialog.tsx
│               ├── DatabaseTabs.tsx
│               ├── FolderSettingsDialog.tsx
│               ├── AuthDialog.tsx
│               ├── CertificateDialog.tsx
│               ├── AccountActionDialog.tsx
│               ├── ManualCodeFlowDialog.tsx
│               └── DeleteDialog.tsx
├── patches/
│   ├── freerdp/            # FreeRDP patches (AAD token parsing)
│   └── webui/              # WebUI patches (OAuth redirect passthrough)
├── scripts/
│   └── generate_vfs.py     # Embeds React assets for single-binary builds
├── subprojects/
│   ├── freerdp/            # FreeRDP git submodule
│   ├── webui/              # WebUI (cloned by init.sh)
│   └── packagefiles/
│       └── webui/
│           └── meson.build
├── init.sh                 # Full clean build script
├── setup-deps.sh           # Dependency installation
├── meson.build             # Main build configuration
├── meson_options.txt       # Build options
├── PATCHES.md              # Patch documentation
└── MODERNIZATION.md        # C++23 migration notes
```

## Configuration

Connection data is stored in a SQLite database:

- **Linux**: `~/.config/webui-rdp-client/connections.db`
- **Windows**: `%APPDATA%/webui-rdp-client/connections.db`
- **macOS**: `~/.config/webui-rdp-client/connections.db`

The application supports multiple databases — you can create, open, clone, and switch between database files.

## Keyboard Shortcuts (in UI)

| Shortcut | Action |
|----------|--------|
| `Ctrl+N` | New connection |
| `F2` | Rename selected folder |
| `Delete` | Delete selected folder |
| `Double-click` | Connect to saved connection |

## Troubleshooting

### "FreeRDP client not found"

Ensure xfreerdp is installed and in your PATH:

```bash
# Check if installed
which xfreerdp3 || which xfreerdp

# Install on Ubuntu/Debian
sudo apt install freerdp2-x11
```

### "Failed to open browser window"

WebUI requires a browser. Install one of:
- Chrome/Chromium
- Firefox
- Edge
- Safari (macOS)

### Build fails with missing dependencies

Run the dependency installation commands for your platform (see Prerequisites).

## License

MIT License - See LICENSE file for details.

## Credits

- [WebUI](https://github.com/webui-dev/webui) - Lightweight web-based UI library
- [FreeRDP](https://github.com/FreeRDP/FreeRDP) - Free RDP client implementation
