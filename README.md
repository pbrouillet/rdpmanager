# WebUI RDP Client

A native C++ Remote Desktop client featuring a modern web-based UI powered by [WebUI](https://github.com/webui-dev/webui) and [FreeRDP](https://github.com/FreeRDP/FreeRDP).

## Features

- 🖥️ Modern, dark-themed web UI
- 🔐 Save and manage RDP connection profiles
- 🚀 Spawn FreeRDP sessions with a single click
- 📋 Clipboard redirection support
- 🔊 Audio redirection support
- 🛡️ Certificate handling (TOFU model)

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Application                          │
├─────────────────────────────────────────────────────────┤
│  ┌───────────────┐    ┌────────────────────────────┐   │
│  │   WebUI       │◄──►│  HTML/CSS/JS Frontend      │   │
│  │   (Browser)   │    │  (src/ui/)                 │   │
│  └───────┬───────┘    └────────────────────────────┘   │
│          │                                              │
│          ▼                                              │
│  ┌───────────────┐    ┌────────────────────────────┐   │
│  │  C++ Backend  │───►│  RDP Launcher              │   │
│  │  (main.cpp)   │    │  (FreeRDP subprocess)      │   │
│  └───────────────┘    └────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## Prerequisites

### Linux (Ubuntu/Debian)

```bash
# Build tools
sudo apt update
sudo apt install -y build-essential meson ninja-build cmake pkg-config git

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

### 1. Clone and Initialize

```bash
git clone <your-repo-url> webui-rdp-client
cd webui-rdp-client

# Initialize FreeRDP submodule
git submodule update --init --recursive
```

### 2. Configure with Meson

```bash
# Debug build
meson setup build --buildtype=debug

# Release build
meson setup build-release --buildtype=release
```

### 3. Build with Ninja

```bash
# Debug
ninja -C build

# Release
ninja -C build-release
```

### 4. Run

```bash
./build/webui-rdp-client
```

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
│   ├── rdp_launcher.cpp    # FreeRDP integration
│   ├── rdp_launcher.hpp
│   ├── config_manager.cpp  # Connection persistence
│   ├── config_manager.hpp
│   ├── ui/                 # Legacy plain HTML UI (fallback)
│   │   ├── index.html
│   │   ├── styles.css
│   │   └── app.js
│   └── ui-react/           # React/TypeScript/Fluent UI frontend
│       ├── package.json
│       ├── tsconfig.json
│       ├── vite.config.ts
│       ├── build-ui.sh     # Build script (called by Meson)
│       └── src/
│           ├── main.tsx
│           ├── App.tsx
│           ├── types.ts
│           ├── api.ts
│           ├── theme.ts
│           └── components/
├── subprojects/
│   ├── freerdp/            # FreeRDP git submodule
│   ├── webui.wrap          # WebUI wrap file
│   └── packagefiles/
│       └── webui/
│           └── meson.build
├── meson.build             # Main build configuration
├── meson_options.txt       # Build options
├── .gitmodules             # Git submodules
└── README.md
```

## Configuration

Connection profiles are stored in:

- **Linux**: `~/.config/webui-rdp-client/connections.json`
- **Windows**: `%APPDATA%/webui-rdp-client/connections.json`
- **macOS**: `~/.config/webui-rdp-client/connections.json`

## Keyboard Shortcuts (in UI)

| Shortcut | Action |
|----------|--------|
| `Ctrl+Enter` | Connect to current server |
| `Ctrl+S` | Save current connection |
| `Escape` | Close modal dialogs |
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
