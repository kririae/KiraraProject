#pragma once

#include <spdlog/common.h>

#include <optional>

#include "kira/Compiler.h"
#include "kira/detail/Logger.h"

namespace kira {
/// The logger name by default.
constexpr detail::StringLiteral const defaultLoggerName = "kira";

/// The builder to create a logger.
class LoggerBuilder {
public:
    LoggerBuilder() noexcept : name(defaultLoggerName) {}
    LoggerBuilder(std::string_view name) noexcept : name(name) {}

public:
    /// Set whether to log to console.
    [[nodiscard]] LoggerBuilder &to_console(bool console) noexcept {
        this->console = console;
        return *this;
    }

    /// Set the file path to log to.
    [[nodiscard]] LoggerBuilder &to_file(std::filesystem::path const &path) noexcept {
        this->path = path;
        return *this;
    }

    [[nodiscard]] LoggerBuilder &filter_level(spdlog::level::level_enum const &level) noexcept {
        this->level = level;
        return *this;
    }

    /// Initializes the logger with the previously specified configuration.
    std::shared_ptr<spdlog::logger> init();

private:
    std::string_view name;
    bool console{true};
    std::optional<std::filesystem::path> path;
    std::optional<spdlog::level::level_enum> level;
};

/// \brief Get or create a thread-safe logger.
///
/// Returns an existing logger if available, or creates a new one with default
/// configuration if not. The logger creation is thread-safe.
///
/// \tparam name The name of the logger as a string literal.
///
/// \return A pointer to the spdlog logger.
/// \throw std::runtime_error If logger initialization fails.
[[nodiscard]] inline spdlog::logger *GetLogger(std::string const &name) {
    static std::mutex mutex;

    std::lock_guard guard(mutex);
    auto logger = spdlog::get(name);
    if (KIRA_UNLIKELY(not logger))
        logger = LoggerBuilder{name}.init();
    return logger.get();
}

//! NOTE(krr): compile-time format check is not yet achieved. Turning into macros? No, you'll lose
//! the ability to pass logger name as a template parameter. Consider this,
//!
//! LogInfo("kiraTheLogger", "Hello, {:s}", "world");
//!
//! It will be ambiguous to determine whether "kiraTheLogger" is the format string or the logger
//! name.
//! Ok, let's try to pass `fmt` directly to the `fmt::format` without using `fmt::runtime`? No,
//! compiler will complain that it is not in consteval context. The only feasible way to do this is
//! to use https://stackoverflow.com/a/78540292 (through `reinterpret_cast` and inheritance to
//! `std::string_view`), but this results in a segfault in our codebase.

/// Log a message at the trace level.
template <detail::StringLiteral name = defaultLoggerName, fmt::formattable<char>... Args>
inline void LogTrace(detail::FormatWithSourceLoc fmt, Args &&...args) {
    GetLogger(name.value)
        ->log(
            detail::get_spdlog_source_loc(fmt.loc), spdlog::level::trace, fmt::runtime(fmt.fmt),
            std::forward<Args>(args)...
        );
}

/// Log a message at the debug level.
template <detail::StringLiteral name = defaultLoggerName, fmt::formattable<char>... Args>
inline void LogDebug(detail::FormatWithSourceLoc fmt, Args &&...args) {
    GetLogger(name.value)
        ->log(
            detail::get_spdlog_source_loc(fmt.loc), spdlog::level::debug, fmt::runtime(fmt.fmt),
            std::forward<Args>(args)...
        );
}

/// Log a message at the info level.
template <detail::StringLiteral name = defaultLoggerName, fmt::formattable<char>... Args>
inline void LogInfo(detail::FormatWithSourceLoc fmt, Args &&...args) {
    GetLogger(name.value)
        ->log(
            detail::get_spdlog_source_loc(fmt.loc), spdlog::level::info, fmt::runtime(fmt.fmt),
            std::forward<Args>(args)...
        );
}

/// Log a message at the warn level.
template <detail::StringLiteral name = defaultLoggerName, fmt::formattable<char>... Args>
inline void LogWarn(detail::FormatWithSourceLoc fmt, Args &&...args) {
    GetLogger(name.value)
        ->log(
            detail::get_spdlog_source_loc(fmt.loc), spdlog::level::warn, fmt::runtime(fmt.fmt),
            std::forward<Args>(args)...
        );
}

/// Log a message at the error level.
template <detail::StringLiteral name = defaultLoggerName, fmt::formattable<char>... Args>
inline void LogError(detail::FormatWithSourceLoc fmt, Args &&...args) {
    GetLogger(name.value)
        ->log(
            detail::get_spdlog_source_loc(fmt.loc), spdlog::level::err, fmt::runtime(fmt.fmt),
            std::forward<Args>(args)...
        );
}

/// Flush the logger.
template <detail::StringLiteral name = defaultLoggerName> inline void LogFlush() {
    GetLogger(name.value)->flush();
}
} // namespace kira
