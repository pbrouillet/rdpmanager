#!/usr/bin/env bash
# Verify that all local patches apply cleanly to their target submodules.
# Used by CI (patch guard workflow) and can be run locally.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PASS=0
FAIL=0

check_patch() {
    local name="$1"
    local submodule_dir="$2"
    local patch_file="$3"
    local marker="$4"
    local marker_file="$5"

    echo "--- Checking: $name ---"

    if [ ! -f "$patch_file" ]; then
        echo "  [ERROR] Missing patch file: $patch_file"
        FAIL=$((FAIL + 1))
        return
    fi

    if [ ! -d "$submodule_dir/.git" ] && [ ! -f "$submodule_dir/.git" ]; then
        echo "  [WARN] Submodule not initialized: $submodule_dir — skipping"
        return
    fi

    # Check if already applied
    if git -C "$submodule_dir" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
        echo "  [OK] Patch already applied"
        PASS=$((PASS + 1))
    elif git -C "$submodule_dir" apply --check "$patch_file" >/dev/null 2>&1; then
        git -C "$submodule_dir" apply --3way "$patch_file"
        echo "  [OK] Patch applied successfully"
        PASS=$((PASS + 1))
    else
        echo "  [ERROR] Patch does not apply cleanly"
        echo "  Attempting dry-run for diagnostics:"
        git -C "$submodule_dir" apply --check "$patch_file" 2>&1 || true
        FAIL=$((FAIL + 1))
        return
    fi

    # Verify marker if provided
    if [ -n "$marker" ] && [ -n "$marker_file" ]; then
        if [ -f "$marker_file" ] && grep -q "$marker" "$marker_file"; then
            echo "  [OK] Marker verified in $marker_file"
        else
            echo "  [ERROR] Marker not found: '$marker' in $marker_file"
            FAIL=$((FAIL + 1))
            return
        fi
    fi
}

# --- FreeRDP AAD patch ---
# Try Cargo submodule first, fall back to Meson submodule
FREERDP_DIR="$ROOT_DIR/crates/freerdp-sys/freerdp"
if [ ! -d "$FREERDP_DIR" ] || { [ ! -d "$FREERDP_DIR/.git" ] && [ ! -f "$FREERDP_DIR/.git" ]; }; then
    FREERDP_DIR="$ROOT_DIR/subprojects/freerdp"
fi
check_patch \
    "FreeRDP AAD fallback parse" \
    "$FREERDP_DIR" \
    "$ROOT_DIR/patches/freerdp/aad-fallback-parse.patch" \
    "Fallback: parse authentication_result directly from payload text." \
    "$FREERDP_DIR/libfreerdp/core/aad.c"

# --- WebUI navigate passthrough patch ---
WEBUI_DIR="$ROOT_DIR/crates/webui-sys/webui"
if [ ! -d "$WEBUI_DIR" ] || { [ ! -d "$WEBUI_DIR/.git" ] && [ ! -f "$WEBUI_DIR/.git" ]; }; then
    WEBUI_DIR="$ROOT_DIR/subprojects/webui"
fi
check_patch \
    "WebUI navigate passthrough" \
    "$WEBUI_DIR" \
    "$ROOT_DIR/patches/webui/navigate-passthrough.patch" \
    "navigate_passthrough" \
    "$WEBUI_DIR/src/webui.c"

# --- Summary ---
echo ""
echo "=== Patch Guard Summary ==="
echo "Passed: $PASS"
echo "Failed: $FAIL"

if [ "$FAIL" -gt 0 ]; then
    echo "RESULT: FAIL"
    exit 1
fi

echo "RESULT: PASS"
