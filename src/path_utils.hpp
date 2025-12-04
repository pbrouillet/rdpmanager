/**
 * Path Utilities - Filesystem path helpers
 */

#ifndef PATH_UTILS_HPP
#define PATH_UTILS_HPP

#include <filesystem>
#include <vector>
#include <string>

namespace fs = std::filesystem;

namespace path_utils {

/**
 * Find the UI directory path by searching common locations
 * @return Absolute path to the UI directory, or empty path if not found
 */
inline fs::path find_ui_path() {
    // Check compile-time path first
    #ifdef UI_PATH
    fs::path compile_path(UI_PATH);
    if (fs::exists(compile_path / "index.html")) {
        return compile_path;
    }
    #endif
    
    // Check relative paths
    std::vector<fs::path> search_paths = {
        "src/ui",
        "../src/ui",
        "../../src/ui",
        "ui",
        "../ui",
    };
    
    for (const auto& path : search_paths) {
        if (fs::exists(path / "index.html")) {
            return fs::absolute(path);
        }
    }
    
    // Check executable directory
    #ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    fs::path exe_dir = fs::path(buffer).parent_path();
    #else
    fs::path exe_dir = fs::read_symlink("/proc/self/exe").parent_path();
    #endif
    
    if (fs::exists(exe_dir / "ui" / "index.html")) {
        return exe_dir / "ui";
    }
    
    return "";
}

} // namespace path_utils

#endif // PATH_UTILS_HPP
