#!/bin/bash
# Initialize and build webui-rdp-client from scratch
# This script cleans everything, reinitializes submodules, and builds the project

set -e  # Exit on error

echo "=========================================="
echo "WebUI RDP Client - Clean Build Script"
echo "=========================================="

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Step 1: Clean build directories
echo ""
echo "Step 1: Cleaning build directories..."
rm -rf build build-release
echo "✓ Build directories cleaned"

# Step 2: Deinitialize and clean submodules
echo ""
echo "Step 2: Cleaning submodules..."
git submodule deinit -f --all 2>/dev/null || true
rm -rf subprojects/freerdp subprojects/webui
echo "✓ Submodules cleaned"

# Step 3: Reinitialize FreeRDP submodule
echo ""
echo "Step 3: Initializing FreeRDP submodule..."
git submodule update --init --recursive
echo "✓ FreeRDP submodule initialized"

# Step 4: Clone WebUI
echo ""
echo "Step 4: Cloning WebUI..."
if [ ! -d "subprojects/webui" ]; then
    git clone https://github.com/webui-dev/webui.git subprojects/webui
    echo "✓ WebUI cloned"
else
    echo "✓ WebUI already exists"
fi

# Step 5: Copy meson.build for WebUI
echo ""
echo "Step 5: Setting up WebUI build configuration..."
if [ -f "subprojects/packagefiles/webui/meson.build" ]; then
    cp subprojects/packagefiles/webui/meson.build subprojects/webui/meson.build
    echo "✓ WebUI meson.build configured"
else
    echo "✗ Error: subprojects/packagefiles/webui/meson.build not found"
    exit 1
fi

# Step 6: Install React UI npm dependencies
echo ""
echo "Step 6: Installing React UI dependencies..."
if command -v npm &>/dev/null; then
    (cd src/ui-react && npm install --no-audit --no-fund --loglevel=error)
    echo "✓ React UI npm dependencies installed"
else
    echo "⚠ npm not found — React UI will not be built (falling back to plain HTML UI)"
fi

# Step 7: Meson setup
echo ""
echo "Step 7: Running Meson setup (debug)..."
meson setup build --buildtype=debug --wipe
echo "✓ Meson setup complete"

# Step 8: Build with Ninja
echo ""
echo "Step 8: Building project..."
ninja -C build
echo "✓ Build complete"

# Step 9: Verify executable
echo ""
echo "=========================================="
if [ -f "build/webui-rdp-client" ]; then
    echo "✓ SUCCESS! Executable built:"
    ls -lh build/webui-rdp-client
else
    echo "✗ ERROR: Executable not found"
    exit 1
fi
echo "=========================================="
echo ""
echo "Build complete! Run with: ./build/webui-rdp-client"
