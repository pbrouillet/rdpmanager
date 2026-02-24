#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FREERDP_DIR="$ROOT_DIR/subprojects/freerdp"
PATCH_FILE="$ROOT_DIR/patches/freerdp/aad-fallback-parse.patch"
TARGET_FILE="$FREERDP_DIR/libfreerdp/core/aad.c"
MARKER='Fallback: parse authentication_result directly from payload text.'

if [ ! -f "$PATCH_FILE" ]; then
    echo "[ERROR] Missing patch file: $PATCH_FILE"
    exit 1
fi

git -C "$ROOT_DIR" submodule update --init --recursive subprojects/freerdp

if git -C "$FREERDP_DIR" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
    echo "[INFO] FreeRDP patch already applied"
elif git -C "$FREERDP_DIR" apply --check "$PATCH_FILE" >/dev/null 2>&1; then
    git -C "$FREERDP_DIR" apply --3way "$PATCH_FILE"
    echo "[INFO] Applied FreeRDP patch"
else
    echo "[ERROR] FreeRDP patch no longer applies cleanly"
    exit 1
fi

if ! grep -q "$MARKER" "$TARGET_FILE"; then
    echo "[ERROR] Patch marker not found in $TARGET_FILE"
    exit 1
fi

echo "[OK] FreeRDP AAD patch guard passed"
