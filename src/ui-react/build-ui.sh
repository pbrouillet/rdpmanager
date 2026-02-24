#!/usr/bin/env bash
# Build the React UI. Called by meson as a custom_target.
# Arguments: $1 = source dir (ui-react), $2 = output dir
set -euo pipefail

SRC_DIR="$1"
OUT_DIR="$2"

cd "$SRC_DIR"

# Install dependencies if needed
if [ ! -d "node_modules" ]; then
    npm install --no-audit --no-fund --loglevel=error
fi

# Build
npx vite build --outDir "$OUT_DIR" --emptyOutDir 2>&1

echo "[RDPMAN] React UI built → $OUT_DIR"
