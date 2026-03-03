/**
 * Logger - Centralised file + console logging
 *
 * Features:
 *   - Always writes to a log file in the XDG config dir (~/.config/webui-rdp-client/app.log)
 *   - Console output controlled by a runtime verbosity level
 *   - Four severity levels: ERROR, WARN, INFO, DEBUG
 *   - Timestamped: "2026-03-03 14:23:01.123 [INFO ] [AAD] message"
 *   - Thread-safe (single mutex for the file stream)
 *
 * CLI:
 *   --log-level=<error|warn|info|debug>   (default: warn)
 *
 * Usage:
 *     LOG_INFO("RDPMAN", "Loaded " << count << " connections");
 *     LOG_ERROR("AAD", "Failed to open auth window");
 */

#pragma once

#include <string>
#include <string_view>
#include <format>
#include <fstream>
#include <mutex>
#include <iostream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <filesystem>
#include <cstdlib>

namespace logger {

enum class Level {
    Error = 0,
    Warn  = 1,
    Info  = 2,
    Debug = 3,
};

constexpr const char* level_str(Level lvl) {
    switch (lvl) {
        case Level::Error: return "ERROR";
        case Level::Warn:  return "WARN ";
        case Level::Info:  return "INFO ";
        case Level::Debug: return "DEBUG";
    }
    return "?????";
}

/**
 * Parse a level name string (case-insensitive).
 * Returns Level::Warn on unrecognised input.
 */
constexpr Level parse_level(std::string_view s) {
    if (s == "error" || s == "ERROR") return Level::Error;
    if (s == "warn"  || s == "WARN")  return Level::Warn;
    if (s == "info"  || s == "INFO")  return Level::Info;
    if (s == "debug" || s == "DEBUG") return Level::Debug;
    return Level::Warn;
}

// ============================================================================
// Singleton logger instance
// ============================================================================

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    /**
     * Initialise the logger.
     * @param console_level  Messages at this level or below are printed to stderr.
     *                       The log file always receives everything (Debug).
     */
    void init(Level console_level = Level::Warn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_console_level = console_level;

        if (!m_file.is_open()) {
            namespace fs = std::filesystem;
            fs::path log_dir = get_log_directory();
            if (!fs::exists(log_dir)) {
                std::error_code ec;
                fs::create_directories(log_dir, ec);
            }
            fs::path log_path = log_dir / "app.log";
            m_file.open(log_path, std::ios::app);
            if (m_file.is_open()) {
                m_file << "\n========== Session started ==========\n";
                m_file.flush();
            }
        }
    }

    void set_console_level(Level lvl) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_console_level = lvl;
    }

    Level console_level() const { return m_console_level; }

    /**
     * Write a log message.
     * Always written to the file; only written to stderr when
     * the message level is <= m_console_level.
     */
    void log(Level lvl, const char* tag, std::string_view message) {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::string line = format(lvl, tag, message);

        // Always write to file
        if (m_file.is_open()) {
            m_file << line << '\n';
            m_file.flush();
        }

        // Conditionally write to console (stderr)
        if (lvl <= m_console_level) {
            std::cerr << line << '\n';
        }
    }

    /**
     * Write raw text to both file and console stdout (no formatting).
     * Used for banners, user prompts, and other special output
     * that should bypass the normal log format.
     */
    void raw(std::string_view text) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_file.is_open()) {
            m_file << text;
            m_file.flush();
        }
        std::cout << text;
        std::cout.flush();
    }

private:
    Logger() = default;
    ~Logger() {
        if (m_file.is_open()) {
            m_file.close();
        }
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static std::filesystem::path get_log_directory() {
#ifdef _WIN32
        const char* appdata = std::getenv("APPDATA");
        if (appdata) {
            return std::filesystem::path(appdata) / "webui-rdp-client";
        }
        return std::filesystem::path(".") / ".webui-rdp-client";
#else
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg) {
            return std::filesystem::path(xdg) / "webui-rdp-client";
        }
        const char* home = std::getenv("HOME");
        if (home) {
            return std::filesystem::path(home) / ".config" / "webui-rdp-client";
        }
        return std::filesystem::path(".") / ".webui-rdp-client";
#endif
    }

    std::string format(Level lvl, const char* tag, std::string_view msg) {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &tt);
#else
        localtime_r(&tt, &tm_buf);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count()
            << " [" << level_str(lvl) << "] "
            << '[' << tag << "] "
            << msg;
        return oss.str();
    }

    std::mutex m_mutex;
    std::ofstream m_file;
    Level m_console_level = Level::Warn;
};

} // namespace logger

// ============================================================================
// Convenience macros — stream-style: LOG_INFO("TAG", "count=" << n);
// ============================================================================

#define LOG_ERROR(tag, msg) do { \
    std::ostringstream _log_ss; _log_ss << msg; \
    ::logger::Logger::instance().log(::logger::Level::Error, tag, _log_ss.str()); \
} while (0)

#define LOG_WARN(tag, msg) do { \
    std::ostringstream _log_ss; _log_ss << msg; \
    ::logger::Logger::instance().log(::logger::Level::Warn, tag, _log_ss.str()); \
} while (0)

#define LOG_INFO(tag, msg) do { \
    std::ostringstream _log_ss; _log_ss << msg; \
    ::logger::Logger::instance().log(::logger::Level::Info, tag, _log_ss.str()); \
} while (0)

#define LOG_DEBUG(tag, msg) do { \
    std::ostringstream _log_ss; _log_ss << msg; \
    ::logger::Logger::instance().log(::logger::Level::Debug, tag, _log_ss.str()); \
} while (0)

// Raw output (banners, user prompts) — bypasses formatting, goes to stdout + file
#define LOG_RAW(text) do { \
    std::ostringstream _log_ss; _log_ss << text; \
    ::logger::Logger::instance().raw(_log_ss.str()); \
} while (0)


