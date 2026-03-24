# Session Tabs — RDP Session Embedding

## 1. Overview

rdpmanager supports running multiple RDP sessions as tabs within the main WebUI window. Each session is managed by a state machine that tracks its lifecycle from launch through disconnection:

**Idle → Connecting → Connected → Disconnecting → Disconnected → Error**

The feature is delivered in two phases:

- **Phase 1 (current):** Sessions run as separate FreeRDP windows. The tab bar tracks session state and allows switching between sessions, but each session occupies its own OS-level window.
- **Phase 2 (planned):** Native window reparenting embeds FreeRDP windows directly inside the WebUI content area, providing a true tabbed experience with a single application window.

## 2. Architecture

```
React SessionTabs ──→ JS bindings ──→ SessionManager ──→ RDPLauncher ──→ FreeRDP thread
                                            │
                                            └──→ EmbeddingHost (Phase 1: stub)
```

### Key Components

| Component | File | Role |
|-----------|------|------|
| `SessionManager` | `src/session_manager.rs` | Coordinator: manages launcher + embedding + tab state |
| `RDPLauncher` | `src/rdp_launcher.rs` | Creates and manages FreeRDP sessions |
| `RDPSession` | `src/rdp_launcher.rs` | Per-session state (UUID, thread, atomic state, context pointer) |
| `EmbeddingHost` | `src/window_embedding.rs` | Platform window reparenting (Phase 1: no-op) |
| `SessionTabs` | `src/ui-react/src/components/SessionTabs.tsx` | React tab bar component |
| `ContentRect` | `src/window_embedding.rs` | Pixel coordinates for session positioning |

## 3. Session State Machine

```
                 launch()
    Idle ──────────→ Connecting
                        │
                        ▼
                    Connected ──→ Disconnecting ──→ Disconnected
                        │                              ▲
                        └────── Error ─────────────────┘
```

States are stored as `Arc<AtomicU8>` for lock-free reads across threads:

```rust
pub enum RDPConnectionState {
    Idle,           // 0
    Connecting,     // 1
    Connected,      // 2
    Disconnecting,  // 3
    Disconnected,   // 4
    Error,          // 5
}
```

Conversion utilities: `state_to_u8()` / `u8_to_state()` / `state_name()`.

## 4. Session Lifecycle

### Connect Flow

1. User double-clicks a connection in the React UI.
2. `handleConnect()` in `App.tsx` calls `connectRDP(jsonParams)`.
3. `on_connect_rdp` handler in `js_handlers.rs`:
   - Parses `ConnectionProfile` from JSON.
   - Calls `SessionManager::connect(profile, rect)`.
4. `SessionManager`:
   - Calls `RDPLauncher::launch(profile)` → generates a UUID `session_id`.
   - Stores `ContentRect` for the session.
   - Sets `active_session` to the new session.
5. `RDPLauncher` (Linux only):
   - Creates a FreeRDP context via `RdpClientEntry()`.
   - Applies settings (hostname, port, credentials, display, channels).
   - Calls `freerdp_client_start(context)`.
   - Spawns a background thread that waits on `WaitForSingleObject(thread_handle, INFINITE)`.
   - State transitions: Connecting → Connected → Disconnected.
6. Returns `{success: true, sessionId: "uuid"}` to React.
7. React adds the session to `activeSessions` state and switches to the session tab.

### Disconnect Flow

1. User clicks the close button on a session tab.
2. React calls `disconnectSession(sessionId)`.
3. `on_disconnect_session` handler calls `SessionManager::disconnect(session_id)`.
4. `SessionManager`:
   - Calls `EmbeddingHost::hide_session()` (no-op in Phase 1).
   - Removes rect, clears `active_session` if it was the active session.
   - Calls `RDPLauncher::disconnect(session_id)`.
5. `RDPLauncher`:
   - Sets state to `Disconnecting`.
   - Calls `freerdp_client_stop(context)`.
   - Waits for the thread to join.
   - Calls `freerdp_client_context_free(context)`.
6. React removes the session from tabs and switches to the home view.

### Tab Switching

- `switchTab(sessionId, rect)` → show the target session, hide all others.
- `showHomeTab()` → hide all sessions, show normal WebView content.
- Each switch updates `active_session` and the `rects` map.

### Resize

- `resizeSession(sessionId, rect)` → update rect, reposition via `EmbeddingHost`.
- Phase 2: Will also send a `DISPLAY_CONTROL_MONITOR_LAYOUT` PDU for dynamic resolution changes.

## 5. FreeRDP Settings

Settings are applied in `src/rdp_launcher.rs` via `apply_settings()`. Macros simplify FreeRDP configuration:

```rust
macro_rules! set_str {
    ($settings:expr, $key:expr, $val:expr) => {
        freerdp_settings_set_string($settings, $key as _, CString::new($val).unwrap().as_ptr())
    };
}
```

### Applied Settings

| Setting | FreeRDP Key | Source |
|---------|------------|--------|
| Hostname | `FreeRDP_ServerHostname` | `profile.hostname` |
| Port | `FreeRDP_ServerPort` | `profile.port` |
| Username | `FreeRDP_Username` | `profile.username` |
| Domain | `FreeRDP_Domain` | `profile.domain` |
| Width | `FreeRDP_DesktopWidth` | `profile.width` |
| Height | `FreeRDP_DesktopHeight` | `profile.height` |
| Fullscreen | `FreeRDP_Fullscreen` | `profile.fullscreen` |
| Clipboard | `FreeRDP_RedirectClipboard` | `profile.clipboard` |
| Dynamic Resolution | `FreeRDP_DynamicResolutionUpdate` | `profile.dynamic_resolution` |
| Display Control | `FreeRDP_SupportDisplayControl` | `profile.dynamic_resolution` |
| Compression | `FreeRDP_CompressionEnabled` | `profile.compression` |
| Auto-Reconnect | `FreeRDP_AutoReconnectionEnabled` | `profile.auto_reconnect` |
| Auto-Accept Cert | `FreeRDP_AutoAcceptCertificate` | `profile.cert_tofu` |
| AAD Security | `FreeRDP_AadSecurity` | `profile.enable_rds_aad_auth` |
| Gateway | `FreeRDP_GatewayHostname` | `profile.gateway_hostname` |
| GFX H264 | `FreeRDP_GfxH264` | `profile.gfx_avc420` |

## 6. React Session Tab Bar

The session tab bar is implemented in `SessionTabs.tsx`.

### Props

```typescript
interface SessionTabsProps {
  sessions: RdpSession[];
  activeTab: string;        // 'home' or sessionId
  onSelectHome: () => void;
  onSelectSession: (id: string) => void;
  onCloseSession: (id: string) => void;
}
```

### RdpSession Type

```typescript
interface RdpSession {
  id: string;
  hostname: string;
  state: 'connecting' | 'connected' | 'disconnecting' | 'disconnected' | 'error';
}
```

### State Badge Colors

| State | Badge Color |
|-------|------------|
| connected | green (success) |
| connecting | blue (informative) |
| disconnecting | orange (warning) |
| error | red (danger) |

### Periodic Polling

`App.tsx` polls `getActiveSessions()` every 2 seconds to update session states in the tab bar. This ensures the UI reflects backend state transitions (e.g., Connecting → Connected) without requiring push notifications.

## 7. Phase 2: Window Embedding Plan

### ContentRect Coordination

```
React getBoundingClientRect() → {x, y, width, height}
  → switchTab(sessionId, rect) or resizeSession(sessionId, rect)
    → EmbeddingHost positions native window at those coordinates
```

The React UI measures the content area using `getBoundingClientRect()` and passes pixel coordinates to the backend. The `EmbeddingHost` uses these coordinates to position and size the embedded FreeRDP window within the WebUI container.

### Platform Strategies

| Platform | Approach | Key API |
|----------|----------|---------|
| Linux | Set `FreeRDP_ParentWindowId` before launch | Native `XReparentWindow` by FreeRDP |
| Windows | Reparent FreeRDP HWND into WebUI container | `SetParent()`, `SetWindowLongPtr(GWL_STYLE, WS_CHILD)`, `MoveWindow()` |
| macOS | Separate windows initially (deferred) | NSView embedding or fallback |

### EmbeddingHost Interface

```rust
pub struct EmbeddingHost {
    webui_window: usize,
}

impl EmbeddingHost {
    pub fn new(webui_window: usize) -> Self;
    pub fn is_embedding_supported() -> bool;
    pub fn get_parent_window_id(&self) -> Option<u64>;
    pub fn show_session(&self, session_id: &str);
    pub fn hide_session(&self, session_id: &str);
    pub fn reposition_session(&self, session_id: &str, rect: &ContentRect);
    pub fn remove_session(&self, session_id: &str);
}
```

### Dynamic Resolution (Phase 2)

- Enable `FreeRDP_DynamicResolutionUpdate` + `FreeRDP_SupportDisplayControl`.
- On resize: send `DISPLAY_CONTROL_MONITOR_LAYOUT` PDU via `DispClientContext::SendMonitorLayout`.
- Debounce resize events (300ms) to avoid flooding the server with layout changes.

## 8. Platform Limitations

- **Linux only** for FreeRDP sessions currently. The FreeRDP platform client integration requires X11 APIs that are only available on Linux.
- macOS and Windows return an error: *"RDP sessions require FreeRDP platform client. Currently only supported on Linux."*
- Phase 2 will enable Windows support via `SetParent` embedding and improve Linux support via `XReparentWindow`.

---

## See Also

- [Architecture](architecture.md) — Overall system architecture and component relationships.
- [WebUI Bindings](webui-bindings.md) — JavaScript ↔ C++ binding layer documentation.
