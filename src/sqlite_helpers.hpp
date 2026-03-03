/**
 * SQLite RAII helpers
 *
 * Provides an RAII wrapper around sqlite3_stmt to prevent statement leaks
 * on early returns or exceptions.
 */

#pragma once

#include <sqlite3.h>
#include <string>

/**
 * RAII wrapper for sqlite3_stmt.
 *
 * Calls sqlite3_prepare_v2() in the constructor and sqlite3_finalize()
 * in the destructor.  Can be used transparently wherever a raw
 * sqlite3_stmt* is expected via the implicit conversion operator.
 *
 * Usage:
 *     SqliteStmt stmt(db, "SELECT * FROM t WHERE id = ?");
 *     if (!stmt) { // handle error }
 *     sqlite3_bind_int(stmt, 1, id);
 *     while (sqlite3_step(stmt) == SQLITE_ROW) { ... }
 *     // sqlite3_finalize() called automatically
 */
class SqliteStmt {
public:
    SqliteStmt(sqlite3* db, const char* sql)
        : m_stmt(nullptr)
    {
        m_rc = sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr);
    }

    ~SqliteStmt() {
        if (m_stmt) {
            sqlite3_finalize(m_stmt);
        }
    }

    // Non-copyable
    SqliteStmt(const SqliteStmt&) = delete;
    SqliteStmt& operator=(const SqliteStmt&) = delete;

    // Movable
    SqliteStmt(SqliteStmt&& other) noexcept
        : m_stmt(other.m_stmt), m_rc(other.m_rc)
    {
        other.m_stmt = nullptr;
    }
    SqliteStmt& operator=(SqliteStmt&& other) noexcept {
        if (this != &other) {
            if (m_stmt) sqlite3_finalize(m_stmt);
            m_stmt = other.m_stmt;
            m_rc = other.m_rc;
            other.m_stmt = nullptr;
        }
        return *this;
    }

    /// True if sqlite3_prepare_v2 succeeded.
    [[nodiscard]] explicit operator bool() const { return m_rc == SQLITE_OK && m_stmt != nullptr; }

    /// Implicit conversion to raw pointer for sqlite3_bind_*/step/column_* APIs.
    operator sqlite3_stmt*() const { return m_stmt; }

    /// Access the raw pointer.
    [[nodiscard]] sqlite3_stmt* get() const { return m_stmt; }

    /// The return code from sqlite3_prepare_v2.
    [[nodiscard]] int prepare_result() const { return m_rc; }

private:
    sqlite3_stmt* m_stmt;
    int m_rc;
};


