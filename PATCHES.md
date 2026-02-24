# Local Patches

This project applies patches to vendored submodules during the build
initialisation step (`init.sh`). Each patch is stored under `patches/<submodule>/`
and is applied automatically when `init.sh` runs. Patches are idempotent — they
are skipped if already applied.

---

## 1. FreeRDP — `patches/freerdp/aad-fallback-parse.patch`

| | |
|---|---|
| **Submodule** | `subprojects/freerdp` |
| **File modified** | `libfreerdp/core/aad.c` |
| **Applied in** | `init.sh` Step 4 |

### Problem

The Azure AD / Entra ID authentication flow in FreeRDP exchanges JSON payloads
with the RD Gateway. Two parsing issues can cause the flow to fail:

1. **Malformed JSON payload** — The initial `ts_nonce` JSON message from the
   server sometimes arrives with trailing garbage bytes that cause
   `WINPR_JSON_ParseWithLength()` to fail, aborting the entire AAD handshake.

2. **`authentication_result` type mismatch** — The AAD gateway may return
   `authentication_result` as either a JSON number (`0`) or a JSON string
   (`"0"`). Upstream FreeRDP's `json_get_number()` only handles the numeric
   case and fails on strings, causing successful authentications to be
   rejected.

### What the patch does

- **`aad_parse_state_initial` fallback** — When JSON parsing fails, the patch
  adds a text-based fallback that extracts the `ts_nonce` value directly from
  the raw payload using string search, allowing the handshake to proceed even
  with malformed JSON.

- **`aad_parse_state_auth` flexible parsing** — Replaces the strict
  `json_get_number()` call with a two-path parser that handles both numeric
  and string-typed `authentication_result` values. A text-based fallback is
  also added for cases where the entire JSON payload fails to parse.

- **Removes the now-unused `json_get_number()` helper** since all call sites
  have been replaced with more flexible parsing.

- **Adds `#include <errno.h>`** required by `strtoul()` error checking.

### Why it is needed

Without this patch, AVD (Azure Virtual Desktop) and Dev Box connections that
use AAD/Entra authentication fail intermittently at the token exchange stage,
even though the OAuth authorization code was obtained successfully.

---

## 2. WebUI — `patches/webui/navigate-passthrough.patch`

| | |
|---|---|
| **Submodule** | `subprojects/webui` |
| **Files modified** | `include/webui.h`, `include/webui.hpp`, `src/webui.c` |
| **Applied in** | `init.sh` Step 6b |

### Problem

WebUI's Linux WebView backend (WebKitGTK) intercepts **all** navigation events
via the `decide-policy` signal. When the application binds the `""` (all-events)
handler, every navigation after the initial page load is **blocked** by
`webkit_policy_decision_ignore()`. The URL is delivered to the application
callback, but the WebView never actually loads the page.

This behaviour is intentional for single-page apps served from WebUI's local
HTTP server, but it breaks multi-step external flows like Microsoft Entra ID
(Azure AD) OAuth login. The login page requires dozens of internal redirects
and form submissions across `login.microsoftonline.com`, `login.live.com`,
`autologon.microsoftazuread-sso.com`, etc. With every sub-navigation blocked,
the user sees a frozen or partially-rendered login page that can never complete.

### What the patch does

Adds a per-window **navigate passthrough** mode controlled by a new API:

```c
// C API
void webui_set_navigate_passthrough(size_t window, bool status);

// C++ wrapper
void webui::window::set_navigate_passthrough(bool status);
```

When enabled on a window:

- The `_webui_wv_event_decision()` handler **skips** the
  `webkit_policy_decision_ignore()` call, allowing WebKitGTK to proceed with
  the navigation normally.
- The navigation URL is still delivered to the application's all-events
  callback, so the app can observe each URL and detect the final OAuth
  redirect containing the authorization code.
- After firing the event, the handler returns `false` (instead of `true`) to
  tell WebKit to use its default navigation policy.

**Changes by file:**

| File | Change |
|---|---|
| `src/webui.c` — `_webui_window_t` struct | Added `bool navigate_passthrough` field |
| `src/webui.c` — `_webui_wv_event_decision()` | Conditional `webkit_policy_decision_ignore()` and return value based on passthrough flag |
| `src/webui.c` — new function | `webui_set_navigate_passthrough()` setter implementation |
| `include/webui.h` | Public C API declaration for `webui_set_navigate_passthrough()` |
| `include/webui.hpp` | C++ wrapper method `window::set_navigate_passthrough()` |

### Why it is needed

Without this patch, the AAD authentication popup window in RDP Manager hangs
indefinitely. The Microsoft login page appears to load (the initial GET is
allowed) but all subsequent navigations — redirects, form submissions, iframe
loads — are silently blocked by WebUI. The user cannot interact with or complete
the login flow. With passthrough enabled, the WebView behaves like a normal
browser for the OAuth flow while the application still intercepts the final
redirect URL to extract the authorization code for FreeRDP.
