/**
 * RDP Launcher Implementation
 * 
 * Uses FreeRDP to spawn RDP client connections.
 * Supports both subprocess spawning (xfreerdp) and library integration.
 */

#include "rdp_launcher.hpp"

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
    #define FREERDP_EXECUTABLE "wfreerdp.exe"
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <signal.h>
    #define FREERDP_EXECUTABLE "xfreerdp3"
    #define FREERDP_EXECUTABLE_FALLBACK "xfreerdp"
#endif

// Try to include FreeRDP headers for version info
// These may not be available if building with subprocess-only approach
#if __has_include(<freerdp/version.h>)
    #include <freerdp/version.h>
    #define HAS_FREERDP_HEADERS 1
#else
    #define HAS_FREERDP_HEADERS 0
#endif

namespace fs = std::filesystem;

// ============================================================================
// RDPSession Implementation
// ============================================================================

RDPSession::RDPSession(const RDPConnectionParams& params)
    : m_params(params)
    , m_state(RDPConnectionState::Disconnected)
    , m_running(false)
    , m_pid(-1)
{
}

RDPSession::~RDPSession() {
    stop();
}

std::vector<std::string> RDPSession::build_command_args() const {
    std::vector<std::string> args;
    
    // Server address
    args.push_back("/v:" + m_params.hostname);
    
    // Port (if non-default)
    if (m_params.port != 3389) {
        args.push_back("/port:" + std::to_string(m_params.port));
    }
    
    // Authentication
    if (!m_params.username.empty()) {
        args.push_back("/u:" + m_params.username);
    }
    if (!m_params.domain.empty()) {
        args.push_back("/d:" + m_params.domain);
    }
    if (!m_params.password.empty()) {
        args.push_back("/p:" + m_params.password);
    }
    
    // Display settings
    if (m_params.fullscreen) {
        args.push_back("/f");
    } else {
        args.push_back("/w:" + std::to_string(m_params.width));
        args.push_back("/h:" + std::to_string(m_params.height));
    }
    args.push_back("/bpp:" + std::to_string(m_params.bpp));
    
    // Features
    if (m_params.clipboard) {
        args.push_back("+clipboard");
    }
    if (m_params.audio) {
        args.push_back("/sound");
        args.push_back("/microphone");
    }
    if (m_params.drive_redirection && !m_params.redirect_drive_path.empty()) {
        args.push_back("/drive:shared," + m_params.redirect_drive_path);
    }
    
    // Security
    if (m_params.ignore_certificate) {
        args.push_back("/cert:ignore");
    } else {
        // Use system certificate store, prompt for unknown
        args.push_back("/cert:tofu");
    }
    
    // Gateway (if specified)
    if (!m_params.gateway_hostname.empty()) {
        args.push_back("/g:" + m_params.gateway_hostname);
        if (m_params.gateway_port != 443) {
            args.push_back("/gp:" + std::to_string(m_params.gateway_port));
        }
    }
    
    // Performance options
    if (m_params.disable_wallpaper) {
        args.push_back("-wallpaper");
    }
    if (m_params.disable_themes) {
        args.push_back("-themes");
    }
    if (m_params.disable_font_smoothing) {
        args.push_back("-fonts");
    }
    
    // Dynamic resolution support
    args.push_back("/dynamic-resolution");
    
    // Title
    args.push_back("/title:" + m_params.hostname);
    
    return args;
}

bool RDPSession::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_running) {
        return false;  // Already running
    }
    
    m_running = true;
    m_state = RDPConnectionState::Connecting;
    
    // Start the session in a separate thread
    m_session_thread = std::thread(&RDPSession::session_thread_func, this);
    
    return true;
}

void RDPSession::session_thread_func() {
    auto args = build_command_args();
    
#ifdef _WIN32
    // Windows: Use CreateProcess
    std::string cmd = FREERDP_EXECUTABLE;
    for (const auto& arg : args) {
        cmd += " " + arg;
    }
    
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    
    if (CreateProcessA(
            nullptr,
            const_cast<char*>(cmd.c_str()),
            nullptr, nullptr,
            FALSE,
            0,
            nullptr, nullptr,
            &si, &pi)) {
        m_pid = static_cast<int>(pi.dwProcessId);
        m_state = RDPConnectionState::Connected;
        
        // Wait for process to exit
        WaitForSingleObject(pi.hProcess, INFINITE);
        
        DWORD exit_code;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        m_state = (exit_code == 0) 
            ? RDPConnectionState::Disconnected 
            : RDPConnectionState::Error;
    } else {
        m_state = RDPConnectionState::Error;
    }
#else
    // Linux/macOS: Use fork/exec
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        std::vector<char*> argv;
        
        // Try xfreerdp3 first, fall back to xfreerdp
        std::string exe_name = FREERDP_EXECUTABLE;
        argv.push_back(const_cast<char*>(exe_name.c_str()));
        
        for (auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        // Try xfreerdp3 first
        execvp(FREERDP_EXECUTABLE, argv.data());
        
        // If that fails, try xfreerdp
        argv[0] = const_cast<char*>(FREERDP_EXECUTABLE_FALLBACK);
        execvp(FREERDP_EXECUTABLE_FALLBACK, argv.data());
        
        // If both fail, exit with error
        std::cerr << "[ERROR] Failed to execute FreeRDP client" << std::endl;
        _exit(127);
    } else if (pid > 0) {
        // Parent process
        m_pid = pid;
        m_state = RDPConnectionState::Connected;
        
        // Wait for child to exit
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            m_state = (exit_code == 0)
                ? RDPConnectionState::Disconnected
                : RDPConnectionState::Error;
        } else {
            m_state = RDPConnectionState::Disconnected;
        }
    } else {
        // Fork failed
        m_state = RDPConnectionState::Error;
    }
#endif
    
    m_running = false;
}

void RDPSession::stop() {
    if (!m_running || m_pid <= 0) {
        return;
    }
    
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, m_pid);
    if (hProcess) {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
    }
#else
    kill(m_pid, SIGTERM);
    
    // Give it a moment to terminate gracefully
    usleep(100000);  // 100ms
    
    // Force kill if still running
    if (m_running) {
        kill(m_pid, SIGKILL);
    }
#endif
    
    // Wait for thread to finish
    if (m_session_thread.joinable()) {
        m_session_thread.join();
    }
}

bool RDPSession::is_active() const {
    return m_running;
}

RDPConnectionState RDPSession::get_state() const {
    return m_state;
}

// ============================================================================
// RDPLauncher Implementation
// ============================================================================

RDPLauncher::RDPLauncher() {
    std::cout << "[RDPLauncher] Initialized. FreeRDP version: " 
              << get_freerdp_version() << std::endl;
}

RDPLauncher::~RDPLauncher() {
    terminate_all();
}

std::string RDPLauncher::find_freerdp_executable() const {
    // Check common locations
    std::vector<std::string> search_paths = {
#ifdef _WIN32
        "wfreerdp.exe",
        "C:\\Program Files\\FreeRDP\\wfreerdp.exe",
        "C:\\Program Files (x86)\\FreeRDP\\wfreerdp.exe",
#else
        "xfreerdp3",
        "xfreerdp",
        "/usr/bin/xfreerdp3",
        "/usr/bin/xfreerdp",
        "/usr/local/bin/xfreerdp3",
        "/usr/local/bin/xfreerdp",
        "/opt/freerdp/bin/xfreerdp",
#endif
    };
    
    // Check if in PATH
    for (const auto& path : search_paths) {
        if (fs::exists(path)) {
            return path;
        }
        
        // Also check PATH environment
#ifdef _WIN32
        char buffer[MAX_PATH];
        if (SearchPathA(nullptr, path.c_str(), nullptr, MAX_PATH, buffer, nullptr)) {
            return buffer;
        }
#else
        std::string which_cmd = "which " + path + " 2>/dev/null";
        FILE* pipe = popen(which_cmd.c_str(), "r");
        if (pipe) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), pipe)) {
                pclose(pipe);
                std::string result(buffer);
                // Remove newline
                result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
                if (!result.empty() && fs::exists(result)) {
                    return result;
                }
            }
            pclose(pipe);
        }
#endif
    }
    
    return "";
}

bool RDPLauncher::launch(const RDPConnectionParams& params) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Verify FreeRDP is available
    std::string exe_path = find_freerdp_executable();
    if (exe_path.empty()) {
        m_last_error = "FreeRDP client not found. Please install xfreerdp or wfreerdp.";
        std::cerr << "[ERROR] " << m_last_error << std::endl;
        return false;
    }
    
    std::cout << "[RDPLauncher] Using FreeRDP at: " << exe_path << std::endl;
    std::cout << "[RDPLauncher] Connecting to: " << params.hostname << ":" << params.port << std::endl;
    
    // Clean up finished sessions
    cleanup_sessions();
    
    // Create new session
    auto session = std::make_shared<RDPSession>(params);
    
    if (!session->start()) {
        m_last_error = "Failed to start RDP session";
        return false;
    }
    
    m_sessions.push_back(session);
    
    // Notify callback if set
    if (m_state_callback) {
        m_state_callback(RDPConnectionState::Connecting, 
                        "Connecting to " + params.hostname);
    }
    
    return true;
}

bool RDPLauncher::launch_embedded(const RDPConnectionParams& params) {
    // This would use the FreeRDP library API directly for embedded rendering
    // More complex implementation - placeholder for now
    m_last_error = "Embedded mode not yet implemented. Use standard launch().";
    return false;
}

std::string RDPLauncher::get_last_error() const {
    return m_last_error;
}

std::string RDPLauncher::get_freerdp_version() const {
#if HAS_FREERDP_HEADERS
    return FREERDP_VERSION_FULL;
#else
    // Try to get version from command line
#ifdef _WIN32
    std::string cmd = "wfreerdp /version 2>&1";
#else
    std::string cmd = "xfreerdp3 /version 2>&1 || xfreerdp /version 2>&1";
#endif
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        char buffer[256];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result += buffer;
        }
        pclose(pipe);
        
        // Parse version from output
        if (result.find("FreeRDP") != std::string::npos) {
            // Extract version number
            size_t pos = result.find("version");
            if (pos != std::string::npos) {
                pos = result.find_first_of("0123456789", pos);
                if (pos != std::string::npos) {
                    size_t end = result.find_first_not_of("0123456789.", pos);
                    return result.substr(pos, end - pos);
                }
            }
        }
    }
    
    return "Unknown";
#endif
}

std::vector<std::shared_ptr<RDPSession>> RDPLauncher::get_active_sessions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::shared_ptr<RDPSession>> active;
    for (const auto& session : m_sessions) {
        if (session->is_active()) {
            active.push_back(session);
        }
    }
    return active;
}

void RDPLauncher::set_state_callback(ConnectionStateCallback callback) {
    m_state_callback = std::move(callback);
}

void RDPLauncher::terminate_all() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (auto& session : m_sessions) {
        session->stop();
    }
    m_sessions.clear();
}

void RDPLauncher::cleanup_sessions() {
    m_sessions.erase(
        std::remove_if(m_sessions.begin(), m_sessions.end(),
            [](const std::shared_ptr<RDPSession>& s) {
                return !s->is_active();
            }),
        m_sessions.end()
    );
}
