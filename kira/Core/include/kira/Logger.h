#pragma once

#include "kira/Macros.h"
#include "kira/detail/Logger.h"

namespace kira {
/// Setup the logger to the config and override the later.
///
/// \tparam name The name of the logger as a string literal.
///
/// \param console Whether to log to console
/// \param path Optional file path to log to
inline void
SetupLogger(std::string_view name, bool console, std::optional<std::filesystem::path> const &path) {
    detail::CreateLogger(name, console, path);
}

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
        logger = detail::CreateLogger(name, true, std::nullopt);
    return logger.get();
}

/// The logger name by default.
constexpr detail::StringLiteral const defaultLoggerName = "kira";

/// Log a message at the trace level.
template <detail::StringLiteral name = defaultLoggerName, typename... Args>
inline void LogTrace(detail::FormatWithSourceLoc fmt, Args &&...args) {
    GetLogger(name.value)
        ->log(
            detail::get_spdlog_source_loc(fmt.loc), spdlog::level::trace, fmt::runtime(fmt.fmt),
            std::forward<Args>(args)...
        );
}

/// Log a message at the debug level.
template <detail::StringLiteral name = defaultLoggerName, typename... Args>
inline void LogDebug(detail::FormatWithSourceLoc fmt, Args &&...args) {
    GetLogger(name.value)
        ->log(
            detail::get_spdlog_source_loc(fmt.loc), spdlog::level::debug, fmt::runtime(fmt.fmt),
            std::forward<Args>(args)...
        );
}

/// Log a message at the info level.
template <detail::StringLiteral name = defaultLoggerName, typename... Args>
inline void LogInfo(detail::FormatWithSourceLoc fmt, Args &&...args) {
    GetLogger(name.value)
        ->log(
            detail::get_spdlog_source_loc(fmt.loc), spdlog::level::info, fmt::runtime(fmt.fmt),
            std::forward<Args>(args)...
        );
}

/// Log a message at the warn level.
template <detail::StringLiteral name = defaultLoggerName, typename... Args>
inline void LogWarn(detail::FormatWithSourceLoc fmt, Args &&...args) {
    GetLogger(name.value)
        ->log(
            detail::get_spdlog_source_loc(fmt.loc), spdlog::level::warn, fmt::runtime(fmt.fmt),
            std::forward<Args>(args)...
        );
}

/// Log a message at the error level.
template <detail::StringLiteral name = defaultLoggerName, typename... Args>
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
