//! Native file picker dialogs (no external crate dependencies).
//!
//! - **Windows**: Win32 `GetOpenFileNameA` / `GetSaveFileNameA` via raw FFI.
//! - **macOS**: `osascript` (AppleScript) subprocesses.
//! - **Linux**: zenity, falling back to kdialog.

// ── Windows ──────────────────────────────────────────────────────────────

#[cfg(target_os = "windows")]
mod platform {
    use std::ffi::{CStr, CString};
    use std::mem;
    use std::path::PathBuf;

    #[repr(C)]
    #[allow(non_snake_case)]
    struct OpenFileNameA {
        lStructSize: u32,
        hwndOwner: *mut std::ffi::c_void,
        hInstance: *mut std::ffi::c_void,
        lpstrFilter: *const i8,
        lpstrCustomFilter: *mut i8,
        nMaxCustFilter: u32,
        nFilterIndex: u32,
        lpstrFile: *mut i8,
        nMaxFile: u32,
        lpstrFileTitle: *mut i8,
        nMaxFileTitle: u32,
        lpstrInitialDir: *const i8,
        lpstrTitle: *const i8,
        Flags: u32,
        nFileOffset: u16,
        nFileExtension: u16,
        lpstrDefExt: *const i8,
        lCustData: isize,
        lpfnHook: *mut std::ffi::c_void,
        lpTemplateName: *const i8,
    }

    const OFN_FILEMUSTEXIST: u32 = 0x0000_1000;
    const OFN_PATHMUSTEXIST: u32 = 0x0000_0800;
    const OFN_OVERWRITEPROMPT: u32 = 0x0000_0002;
    const OFN_NOCHANGEDIR: u32 = 0x0000_0008;

    extern "system" {
        fn GetOpenFileNameA(lpofn: *mut OpenFileNameA) -> i32;
        fn GetSaveFileNameA(lpofn: *mut OpenFileNameA) -> i32;
    }

    /// Filter buffer with embedded NUL separators for OPENFILENAMEA.
    /// Format: pairs of (description, pattern) separated by NUL, double-NUL terminated.
    /// Returns the raw bytes and a pointer that remains valid while the Vec is alive.
    fn db_filter() -> Vec<i8> {
        let raw = b"Database Files\0*.db;*.sqlite;*.sqlite3\0All Files\0*.*\0\0";
        raw.iter().map(|&b| b as i8).collect()
    }

    pub fn pick_open_file(title: &str) -> Option<PathBuf> {
        unsafe {
            let mut file_buf = vec![0i8; 260];
            let filter = db_filter();
            let title_c = CString::new(title).ok()?;

            let mut ofn: OpenFileNameA = mem::zeroed();
            ofn.lStructSize = mem::size_of::<OpenFileNameA>() as u32;
            ofn.lpstrFilter = filter.as_ptr();
            ofn.lpstrFile = file_buf.as_mut_ptr();
            ofn.nMaxFile = 260;
            ofn.lpstrTitle = title_c.as_ptr();
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

            if GetOpenFileNameA(&mut ofn) != 0 {
                let path = CStr::from_ptr(file_buf.as_ptr())
                    .to_string_lossy()
                    .to_string();
                if path.is_empty() {
                    None
                } else {
                    Some(PathBuf::from(path))
                }
            } else {
                None
            }
        }
    }

    pub fn pick_save_file(title: &str, default_name: &str) -> Option<PathBuf> {
        unsafe {
            // Pre-fill the buffer with the default file name.
            let mut file_buf = vec![0i8; 260];
            let name_bytes = default_name.as_bytes();
            let copy_len = name_bytes.len().min(259);
            for (i, &b) in name_bytes[..copy_len].iter().enumerate() {
                file_buf[i] = b as i8;
            }

            let filter = db_filter();
            let title_c = CString::new(title).ok()?;
            let ext_c = CString::new("db").ok()?;

            let mut ofn: OpenFileNameA = mem::zeroed();
            ofn.lStructSize = mem::size_of::<OpenFileNameA>() as u32;
            ofn.lpstrFilter = filter.as_ptr();
            ofn.lpstrFile = file_buf.as_mut_ptr();
            ofn.nMaxFile = 260;
            ofn.lpstrTitle = title_c.as_ptr();
            ofn.lpstrDefExt = ext_c.as_ptr();
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

            if GetSaveFileNameA(&mut ofn) != 0 {
                let path = CStr::from_ptr(file_buf.as_ptr())
                    .to_string_lossy()
                    .to_string();
                if path.is_empty() {
                    None
                } else {
                    Some(PathBuf::from(path))
                }
            } else {
                None
            }
        }
    }
}

// ── macOS ────────────────────────────────────────────────────────────────

#[cfg(target_os = "macos")]
mod platform {
    use std::path::PathBuf;
    use std::process::Command;

    fn run_osascript(script: &str) -> Option<PathBuf> {
        let output = Command::new("osascript")
            .args(["-e", script])
            .output()
            .ok()?;
        if output.status.success() {
            let s = String::from_utf8_lossy(&output.stdout).trim().to_string();
            if s.is_empty() {
                None
            } else {
                Some(PathBuf::from(s))
            }
        } else {
            None
        }
    }

    pub fn pick_open_file(title: &str) -> Option<PathBuf> {
        let script = format!(
            r#"POSIX path of (choose file with prompt "{}" of type {{"public.database", "public.data"}})"#,
            title
        );
        run_osascript(&script)
    }

    pub fn pick_save_file(title: &str, default_name: &str) -> Option<PathBuf> {
        let script = format!(
            r#"POSIX path of (choose file name with prompt "{}" default name "{}")"#,
            title, default_name
        );
        run_osascript(&script)
    }
}

// ── Linux ────────────────────────────────────────────────────────────────

#[cfg(target_os = "linux")]
mod platform {
    use std::path::PathBuf;
    use std::process::{Command, Output};

    fn extract_path(output: &Output) -> Option<PathBuf> {
        if output.status.success() {
            let s = String::from_utf8_lossy(&output.stdout).trim().to_string();
            if s.is_empty() {
                None
            } else {
                Some(PathBuf::from(s))
            }
        } else {
            None
        }
    }

    pub fn pick_open_file(title: &str) -> Option<PathBuf> {
        if let Some(path) = Command::new("zenity")
            .args([
                "--file-selection",
                &format!("--title={title}"),
                "--file-filter=Database files | *.db *.sqlite *.sqlite3",
                "--file-filter=All files | *",
            ])
            .output()
            .ok()
            .and_then(|o| extract_path(&o))
        {
            return Some(path);
        }
        Command::new("kdialog")
            .args([
                "--getopenfilename",
                "~",
                "*.db *.sqlite *.sqlite3|Database files",
            ])
            .output()
            .ok()
            .and_then(|o| extract_path(&o))
    }

    pub fn pick_save_file(title: &str, default_name: &str) -> Option<PathBuf> {
        if let Some(path) = Command::new("zenity")
            .args([
                "--file-selection",
                "--save",
                "--confirm-overwrite",
                &format!("--title={title}"),
                &format!("--filename={default_name}"),
            ])
            .output()
            .ok()
            .and_then(|o| extract_path(&o))
        {
            return Some(path);
        }
        Command::new("kdialog")
            .args([
                "--getsavefilename",
                "~",
                "*.db *.sqlite *.sqlite3|Database files",
            ])
            .output()
            .ok()
            .and_then(|o| extract_path(&o))
    }
}

// ── Public API ───────────────────────────────────────────────────────────

pub use platform::pick_open_file;
pub use platform::pick_save_file;
