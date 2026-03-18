#!/bin/bash
# setup-deps.sh — Idempotent script to install all build dependencies
# for webui-rdp-client. Run this once before building with init.sh.
#
# The script tries sudo apt first (fast path). If sudo is unavailable it
# falls back to downloading .deb packages and extracting headers/libs into
# a local prefix, then exports the environment variables that meson/cmake
# need to find them.
#
# Usage:
#   source ./setup-deps.sh        # install deps AND export env vars
#   ./setup-deps.sh               # install deps only (prints env vars)

set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
LOCAL_PREFIX="${LOCAL_PREFIX:-/tmp/rdpmanager-devlibs}"

# ============================================================================
# Helper functions
# ============================================================================
log()  { echo -e "\033[1;34m==>\033[0m $*"; }
ok()   { echo -e "  \033[1;32m✓\033[0m $*"; }
warn() { echo -e "  \033[1;33m!\033[0m $*"; }
err()  { echo -e "  \033[1;31m✗\033[0m $*" >&2; }

# Check if a command exists
has() { command -v "$1" &>/dev/null; }

# ============================================================================
# 1. Python build tools: meson, ninja, cmake, pkg-config
# ============================================================================
install_python_tools() {
    log "Checking Python build tools (meson, ninja, cmake, pkg-config)..."

    local need_install=()
    has meson    || need_install+=(meson)
    has ninja    || need_install+=(ninja)
    has cmake    || need_install+=(cmake)
    has pkg-config || need_install+=(pkgconf)

    if [ ${#need_install[@]} -eq 0 ]; then
        ok "All Python build tools already installed"
        return 0
    fi

    log "Installing: ${need_install[*]}"

    # Ensure pip is available
    if ! python3 -m pip --version &>/dev/null; then
        log "Bootstrapping pip..."
        curl -sSL https://bootstrap.pypa.io/get-pip.py -o /tmp/get-pip.py
        python3 /tmp/get-pip.py --user --break-system-packages --quiet 2>/dev/null \
            || python3 /tmp/get-pip.py --user --quiet
        rm -f /tmp/get-pip.py
    fi

    local pip_cmd
    if has pip; then
        pip_cmd=pip
    elif has pip3; then
        pip_cmd=pip3
    else
        pip_cmd="$HOME/.local/bin/pip"
    fi

    "$pip_cmd" install --user --break-system-packages --quiet "${need_install[@]}" 2>/dev/null \
        || "$pip_cmd" install --user --quiet "${need_install[@]}"

    # Ensure ~/.local/bin is on PATH for this session
    export PATH="$HOME/.local/bin:$PATH"

    for tool in meson ninja cmake pkg-config; do
        if has "$tool"; then
            ok "$tool $(command -v "$tool")"
        else
            err "$tool not found after install"
            return 1
        fi
    done
}

# ============================================================================
# 2. C/C++ development libraries
# ============================================================================

# All -dev packages needed to build FreeRDP + webui-rdp-client
APT_DEV_PACKAGES=(
    # Core
    zlib1g-dev
    libssl-dev
    libjansson-dev
    libcjson-dev
    # X11
    libx11-dev
    libxext-dev
    libxcb1-dev
    libxau-dev
    libxdmcp-dev
    libxi-dev
    libxrandr-dev
    libxrender-dev
    libxfixes-dev
    libxcursor-dev
    libxinerama-dev
    x11proto-dev
    # Multimedia
    libavcodec-dev
    libavutil-dev
    libavformat-dev
    libswresample-dev
    libswscale-dev
    # System
    libasound2-dev
    libcups2-dev
    libusb-1.0-0-dev
    # Kerberos & ICU
    libkrb5-dev
    krb5-multidev
    comerr-dev
    libicu-dev
    # Compression
    libzstd-dev
    # HTTP / XML (WVD feed discovery)
    libcurl4-openssl-dev
    libpugixml-dev
    # WebKit/GIO TLS backend for embedded WebView HTTPS navigation
    glib-networking
    ca-certificates
    # XCB extras
    libxcb-shm0-dev
    libxcb-xfixes0-dev
    libxcb-render0-dev
    libxcb-shape0-dev
)

# Check whether dev packages are installed via dpkg
dev_packages_installed() {
    for pkg in "${APT_DEV_PACKAGES[@]}"; do
        if ! dpkg -s "$pkg" &>/dev/null; then
            return 1
        fi
    done
    return 0
}

# Fast path: install via sudo apt
install_dev_packages_apt() {
    log "Installing dev packages via apt..."
    sudo apt-get update -qq
    sudo apt-get install -y -qq "${APT_DEV_PACKAGES[@]}"
    ok "Dev packages installed via apt"
}

# Slow path: download .debs and extract to local prefix
install_dev_packages_local() {
    log "sudo unavailable — installing dev packages to local prefix: $LOCAL_PREFIX"

    local dldir="$LOCAL_PREFIX/_debs"
    mkdir -p "$dldir" "$LOCAL_PREFIX"

    # Download packages (apt-get download doesn't need sudo)
    cd "$dldir"
    local to_download=()
    for pkg in "${APT_DEV_PACKAGES[@]}"; do
        # Skip if we already extracted this package marker
        if [ -f "$LOCAL_PREFIX/.installed-$pkg" ]; then
            continue
        fi
        to_download+=("$pkg")
    done

    if [ ${#to_download[@]} -eq 0 ]; then
        ok "All dev packages already extracted to $LOCAL_PREFIX"
        cd "$SCRIPT_DIR"
        return 0
    fi

    log "Downloading ${#to_download[@]} packages..."
    apt-get download "${to_download[@]}" 2>/dev/null

    # Extract each .deb
    for deb in *.deb; do
        [ -f "$deb" ] || continue
        dpkg-deb -x "$deb" "$LOCAL_PREFIX/"
        # Mark as installed
        local name
        name=$(dpkg-deb -f "$deb" Package)
        touch "$LOCAL_PREFIX/.installed-$name"
        rm -f "$deb"
    done
    cd "$SCRIPT_DIR"

    ok "Dev packages extracted to $LOCAL_PREFIX"

    # Fix .so symlinks and remove problematic static archives.
    # Extracted .deb symlinks often point at versioned .so files that only
    # exist in the system libdir. Re-point broken ones at the real copies.
    log "Fixing library symlinks..."
    local libdir="$LOCAL_PREFIX/usr/lib/x86_64-linux-gnu"
    local syslib="/usr/lib/x86_64-linux-gnu"

    if [ -d "$libdir" ]; then
        # Fix every broken .so symlink
        for link in "$libdir"/*.so; do
            [ -L "$link" ] || continue   # skip non-symlinks
            [ -e "$link" ] && continue   # skip working symlinks
            # Broken symlink — find the versioned .so on the system
            local base
            base=$(basename "$link" .so)
            local sys_target
            sys_target=$(ls "$syslib/${base}".so.* 2>/dev/null | head -1) || true
            if [ -n "$sys_target" ]; then
                ln -sf "$sys_target" "$link"
            fi
        done

        # Remove static .a files for system libraries to force shared linking.
        # Static archives pull in transitive deps (zstd, xcb, etc.) that
        # complicate the link. The FreeRDP subproject .a files are fine — only
        # the system library .a files cause issues.
        local remove_static=(
            libcrypto libssl libX11 libXext libXcursor libXfixes libXi
            libXinerama libXrandr libXrender libavcodec libavformat libavutil
            libswresample libswscale libicudata libicui18n libicuio libicuuc
            libjansson libcups libusb-1.0 libcom_err libz libzstd
        )
        for base in "${remove_static[@]}"; do
            rm -f "$libdir/${base}.a"
        done

        # Ensure mit-krb5 directory has proper .so symlinks
        local krb5dir="$libdir/mit-krb5"
        mkdir -p "$krb5dir"
        [ -e "$krb5dir/libkrb5.so" ]    || ln -sf "$syslib/libkrb5.so.3"     "$krb5dir/libkrb5.so"    2>/dev/null || true
        [ -e "$krb5dir/libk5crypto.so" ] || ln -sf "$syslib/libk5crypto.so.3" "$krb5dir/libk5crypto.so" 2>/dev/null || true
        [ -e "$krb5dir/libcom_err.so" ]  || ln -sf "$syslib/libcom_err.so.2"  "$krb5dir/libcom_err.so"  2>/dev/null || true
    fi

    ok "Library symlinks fixed"
}

install_dev_packages() {
    log "Checking C/C++ development libraries..."

    # If all dev packages are already installed system-wide, nothing to do
    if dev_packages_installed; then
        ok "All dev packages already installed system-wide"
        return 0
    fi

    # If a local prefix already has everything, nothing to do
    if [ -d "$LOCAL_PREFIX/usr/include" ]; then
        local all_present=true
        for pkg in "${APT_DEV_PACKAGES[@]}"; do
            if [ ! -f "$LOCAL_PREFIX/.installed-$pkg" ]; then
                all_present=false
                break
            fi
        done
        if $all_present; then
            ok "All dev packages already in local prefix"
            return 0
        fi
    fi

    # Try sudo first
    if sudo -n true 2>/dev/null; then
        install_dev_packages_apt
    else
        install_dev_packages_local
    fi
}

# ============================================================================
# 3. Export environment variables for local prefix
# ============================================================================
export_local_env() {
    # Only needed when using the local prefix (no system-wide -dev packages)
    if dev_packages_installed; then
        return 0
    fi

    if [ ! -d "$LOCAL_PREFIX/usr/include" ]; then
        return 0
    fi

    local libdir="$LOCAL_PREFIX/usr/lib/x86_64-linux-gnu"
    local incdir="$LOCAL_PREFIX/usr/include"
    local archincdir="$LOCAL_PREFIX/usr/include/x86_64-linux-gnu"

    export PATH="$LOCAL_PREFIX/usr/bin:$HOME/.local/bin:$PATH"
    export PKG_CONFIG_PATH="$libdir/pkgconfig:$libdir/pkgconfig/mit-krb5:${PKG_CONFIG_PATH:-}"
    export CMAKE_PREFIX_PATH="$LOCAL_PREFIX/usr"
    export CFLAGS="-I$incdir -I$archincdir ${CFLAGS:-}"
    export CXXFLAGS="-I$incdir -I$archincdir ${CXXFLAGS:-}"
    export LDFLAGS="-L$libdir ${LDFLAGS:-}"

    ok "Environment variables exported for local prefix"
}

# ============================================================================
# 4. Node.js / npm (for React UI build)
# ============================================================================
install_node() {
    log "Checking Node.js and npm..."

    if has node && has npm; then
        ok "node $(node --version), npm $(npm --version)"
        return 0
    fi

    # Try system package manager
    if sudo -n true 2>/dev/null; then
        log "Installing Node.js via apt..."
        if ! dpkg -s nodejs &>/dev/null; then
            sudo apt-get update -qq
            sudo apt-get install -y -qq nodejs npm
        fi
    fi

    if has node && has npm; then
        ok "node $(node --version), npm $(npm --version)"
    else
        warn "Node.js/npm not found — React UI will not be built (plain HTML fallback)"
    fi
}

# ============================================================================
# 5. Git submodules
# ============================================================================
init_submodules() {
    log "Checking git submodules..."
    cd "$SCRIPT_DIR"

    # FreeRDP submodule
    if [ ! -f "subprojects/freerdp/CMakeLists.txt" ]; then
        log "Initializing FreeRDP submodule..."
        git submodule update --init --recursive subprojects/freerdp
        ok "FreeRDP submodule initialized"
    else
        ok "FreeRDP submodule already present"
    fi
}

# ============================================================================
# Main
# ============================================================================
main() {
    echo "=========================================="
    echo "  webui-rdp-client — Dependency Setup"
    echo "=========================================="
    echo ""

    install_python_tools
    echo ""
    install_dev_packages
    echo ""
    install_node
    echo ""
    export_local_env
    echo ""
    init_submodules

    echo ""
    echo "=========================================="
    log "All dependencies ready!"
    echo ""
    echo "  To build:"
    if ! dev_packages_installed && [ -d "$LOCAL_PREFIX/usr/include" ]; then
        echo "    source ./setup-deps.sh   # export env vars"
        echo "    ./init.sh                # build"
        echo ""
        echo "  Or in one shot:"
        echo "    source ./setup-deps.sh && ./init.sh"
    else
        echo "    ./init.sh"
    fi
    echo "=========================================="
}

main "$@"
