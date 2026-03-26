# Upstream Patch Management

## Overview

rdpmanager vendors [FreeRDP](https://github.com/FreeRDP/FreeRDP) and [WebUI](https://github.com/webui-dev/webui) as git submodules and applies local patches to fix issues not yet resolved upstream. This document describes the patch system, CI guardrails, and workflows for managing upstream updates.

## Submodule Layout

| Submodule | Path | Used By |
|-----------|------|---------|
| FreeRDP | `crates/freerdp-sys/freerdp` | Cargo build (primary) |
| FreeRDP | `subprojects/freerdp` | Meson build (legacy) |
| WebUI | `crates/webui-sys/webui` | Cargo build (primary) |

Both FreeRDP entries point to the same upstream commit. The Cargo paths are canonical.

## Local Patches

### FreeRDP: AAD Fallback Parse
- **File:** `patches/freerdp/aad-fallback-parse.patch`
- **Target:** `libfreerdp/core/aad.c`
- **Problem:** Azure AD/Entra ID auth fails intermittently due to:
  1. Malformed JSON payloads from RD Gateway (trailing garbage bytes)
  2. `authentication_result` sometimes returned as string `"0"` instead of number `0`
- **Fix:** Text-based fallback extraction + flexible type handling
- **Marker:** `"Fallback: parse authentication_result directly from payload text."`

### WebUI: Navigate Passthrough
- **File:** `patches/webui/navigate-passthrough.patch`
- **Target:** `include/webui.h`, `include/webui.hpp`, `src/webui.c`
- **Problem:** WebUI blocks all WebView navigation after initial page load, breaking OAuth redirect chains
- **Fix:** Adds `webui_set_navigate_passthrough()` API to conditionally allow navigation
- **Marker:** `"navigate_passthrough"` in `src/webui.c`

## Patch Storage

Patches are stored in two locations:
```
patches/                           ← Primary (used by init.sh, CI scripts)
├── freerdp/aad-fallback-parse.patch
└── webui/navigate-passthrough.patch

crates/freerdp-sys/patches/        ← Crate-local copy (used by Cargo build.rs)
└── aad-fallback-parse.patch

crates/webui-sys/patches/          ← Crate-local copy (used by Cargo build.rs)
└── navigate-passthrough.patch
```

Both copies must be kept in sync. The primary location is `patches/`.

## How Patches Are Applied

### During Cargo Build (automatic)
Each `build.rs` applies its crate-local patch automatically:
1. Checks if patch exists and submodule has `.git`
2. Runs `git apply --check` (dry-run)
3. If clean, applies with `git apply --3way`
4. Silently skips if already applied or patch file missing

### During init.sh (explicit)
The legacy Meson build path applies patches from `patches/`:
1. Checks forward and reverse apply
2. Fails hard (`exit 1`) if patch cannot be applied
3. Reports status to stdout

## CI/CD Workflows

### Patch Guard (`freerdp-patch-guard.yml`)
- **Trigger:** Every push and PR
- **Action:** Inits both submodules, runs `scripts/check-patches.sh`
- **Purpose:** Prevent merging code that breaks patch application
- **Script:** `scripts/check-patches.sh` verifies both FreeRDP and WebUI patches apply cleanly and checks for expected markers in target files

### Upstream Freshness Check (`upstream-check.yml`)
- **Trigger:** Nightly at 06:00 UTC + manual dispatch
- **Action:** Clones latest upstream HEAD for FreeRDP and WebUI, tests patches
- **On conflict:** Opens a GitHub Issue labeled `upstream-patch` with details
- **On success:** Silent pass (no noise)
- **Purpose:** Early warning when upstream changes break our patches

### Upstream Bump (`upstream-bump.yml`)
- **Trigger:** Manual dispatch
- **Inputs:** Target (freerdp/webui), ref (tag/commit/branch)
- **Action:**
  1. Updates submodule to specified ref
  2. Re-applies local patch with `--3way`
  3. Creates branch `deps/bump-{target}-{ref}`
  4. Opens a PR if patches apply cleanly
  5. Fails with conflict diagnostics if not
- **Purpose:** Automated submodule updates with build validation

### Submit Patch Upstream (`upstream-submit.yml`)
- **Trigger:** Manual dispatch
- **Inputs:** Target (freerdp/webui), fork owner, base branch
- **Prerequisites:**
  - Fork the upstream repo on GitHub
  - Create `UPSTREAM_PAT` secret with `repo` scope
- **Action:** Clones fork, applies patch as commit, pushes branch, opens PR against upstream
- **Purpose:** Streamline contributing patches back to upstream projects

## Common Tasks

### Updating FreeRDP to a new release
```bash
# Option 1: Use the Upstream Bump workflow
# Go to Actions → Upstream Bump → Run workflow
# Set target=freerdp, ref=v3.25.0

# Option 2: Manual
cd crates/freerdp-sys/freerdp
git fetch origin
git checkout v3.25.0
cd ../../..
# Re-apply patch
git -C crates/freerdp-sys/freerdp apply --3way patches/freerdp/aad-fallback-parse.patch
# Test
cargo build
git add -A && git commit -m "deps: bump FreeRDP to v3.25.0"
```

### Rebasing a patch after upstream conflict
```bash
# 1. Update submodule to latest
cd crates/freerdp-sys/freerdp && git fetch origin && git checkout origin/master && cd ../../..

# 2. Attempt apply (will show conflicts)
git -C crates/freerdp-sys/freerdp apply --3way patches/freerdp/aad-fallback-parse.patch

# 3. Resolve conflicts in the submodule
cd crates/freerdp-sys/freerdp
# ... edit conflicting files ...
git add -A

# 4. Regenerate the patch
git diff HEAD > ../../../patches/freerdp/aad-fallback-parse.patch
# Also copy to crate-local
cp ../../../patches/freerdp/aad-fallback-parse.patch patches/

# 5. Verify
bash scripts/check-patches.sh
```

### Submitting a patch upstream
1. Fork the upstream repo (e.g., FreeRDP/FreeRDP → youruser/FreeRDP)
2. Add `UPSTREAM_PAT` secret to your rdpmanager repo (GitHub PAT with `repo` scope)
3. Run the **Submit Patch Upstream** workflow with your fork owner

## Design Decisions

- **Nightly, not per-push** for freshness checks — avoids rate-limiting and unnecessary noise
- **GitHub Issues for alerts** — more visible than failed CI runs in a sea of green checks
- **Manual dispatch for bumps/submissions** — these change dependencies and need human review
- **`--3way` merge strategy** — provides readable conflict markers for manual resolution
- **Dual patch storage** — `patches/` is the primary source; crate-local copies exist because Cargo `build.rs` runs in isolation without access to workspace root

---

See also: [Architecture](architecture.md) | [Debugging](debugging.md) | [PATCHES.md](../PATCHES.md)
