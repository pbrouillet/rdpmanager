#!/usr/bin/env bash
# Build the React UI using Rollup. Called by meson as a custom_target.
# Arguments: $1 = source dir (ui-react), $2 = output dir
set -euo pipefail

SRC_DIR="$1"
OUT_DIR="$2"

cd "$SRC_DIR"

# Install dependencies if needed
if [ ! -d "node_modules" ]; then
    npm install --no-audit --no-fund --loglevel=error
fi

# Build with Rollup, passing the output directory
ROLLUP_OUT_DIR="$OUT_DIR" npx rollup -c rollup.config.mjs 2>&1

echo "[RDPMAN] React UI built → $OUT_DIR"
