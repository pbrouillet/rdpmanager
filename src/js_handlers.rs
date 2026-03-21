//! WebUI JavaScript ↔ Rust binding layer.
//!
//! Registers 30 JavaScript functions with WebUI that bridge the React
//! frontend to the Rust backend. Each binding extracts parameters from
//! the WebUI event, delegates to the appropriate manager, and returns
//! a JSON response.
//!
//! Equivalent to: `src/js_handlers.cpp` / `js_handlers.hpp`

// TODO: Implement JSHandlers with bind_all(window) that registers:
//
// RDP Connection Management:
//   connectRDP, getConnections, saveConnection, deleteConnection,
//   getAppInfo, importRdpFile
//
// Database Management:
//   createDatabase, createDatabaseDialog, openDatabase, openDatabaseDialog,
//   cloneDatabase, closeDatabase, getDatabaseStatus
//
// Folder Management:
//   createFolder, moveFolder, renameFolder, deleteFolder, getFolders
//
// Dialog Responses:
//   certificateResponse, authResponse, aadAuthResponse
//
// Feed Discovery:
//   getFeedAccounts, deleteFeedAccount, discoverFeeds,
//   logOffAccount, forgetAccount
//
// Folder Settings & Inheritance:
//   getFolderSettings, saveFolderSettings,
//   getEffectiveFolderSettings, getEffectiveConnectionProfile
