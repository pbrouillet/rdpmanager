//! SQLite persistence layer for connections, folders, tokens, and feed accounts.
//!
//! Manages the `connections.db` SQLite database with tables for:
//! - `connections` — RDP connection profiles (JSON blob per entry)
//! - `folders` — Hierarchical folder organization
//! - `folder_settings` — Sparse parameter inheritance settings
//! - `feed_accounts` — WVD/AVD account metadata
//! - `token_cache` — OAuth token caching with expiration
//!
//! Supports multiple databases (create, open, clone, switch).
//!
//! Equivalent to: `src/config_manager.cpp` / `config_manager.hpp`

// TODO: Implement ConfigManager with:
// - rusqlite::Connection management
// - ensure_schema() — CREATE TABLE IF NOT EXISTS for all 5 tables
// - Connection CRUD (save, delete, get, list as JSON)
// - Folder CRUD (create, move, rename, delete with cascade)
// - FolderSettings with sparse JSON (Option<T> fields)
// - Effective settings resolution (walk ancestor folder chain)
// - Feed account management
// - Token cache (lookup with expiration check, store with UPSERT)
// - Multi-database support (create, open, clone, close)
// - Platform-specific data directory (dirs crate)
