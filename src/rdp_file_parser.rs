//! .rdp file import and parsing.
//!
//! Parses RDP file content (INI-like format) into ConnectionProfile fields.
//! Used for manual RDP file import and WVD feed resource import.
//!
//! Equivalent to: `src/rdp_file_parser.cpp` / `rdp_file_parser.hpp`

// TODO: Implement RDPFileParser with:
// - parse_content(content: &str) -> ParsedRdpFile
// - Field mapping from RDP keys to ConnectionProfile fields
//   (full address, username, domain, screen mode id, desktopwidth, etc.)
// - RemoteApp GUID extraction
// - Gateway settings parsing
