#pragma once

#include "kira/detail/Logger.h"

namespace kira {
/// Setup the logger to the config and override the later.
///
/// \param console Whether to log to console
/// \param path Optional file path to log to
inline void SetupLogger(bool console, std::optional<std::filesystem::path> const &path) {
    detail::CreateSinks(console, path);
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
template <detail::StringLiteral name> [[nodiscard]] inline spdlog::logger *GetLogger() {
    static std::shared_ptr<spdlog::logger> cachedLogger = [] {
        auto logger = spdlog::get(name.value);
        if (not logger)
            logger = detail::CreateLogger(name.value, true, std::nullopt);
        return logger;
    }();

    return cachedLogger.get();
}

/// Log a message at the trace level.
template <detail::StringLiteral name = "kira", typename... Args>
inline void LogTrace(detail::FormatWithLocation fmt, Args &&...args) {
    GetLogger<name>()->log(
        detail::get_spdlog_source_loc(fmt.loc), spdlog::level::trace, fmt::runtime(fmt.fmt),
        std::forward<Args>(args)...
    );
}

/// Log a message at the debug level.
template <detail::StringLiteral name = "kira", typename... Args>
inline void LogDebug(detail::FormatWithLocation fmt, Args &&...args) {
    GetLogger<name>()->log(
        detail::get_spdlog_source_loc(fmt.loc), spdlog::level::debug, fmt::runtime(fmt.fmt),
        std::forward<Args>(args)...
    );
}

/// Log a message at the info level.
template <detail::StringLiteral name = "kira", typename... Args>
inline void LogInfo(detail::FormatWithLocation fmt, Args &&...args) {
    GetLogger<name>()->log(
        detail::get_spdlog_source_loc(fmt.loc), spdlog::level::info, fmt::runtime(fmt.fmt),
        std::forward<Args>(args)...
    );
}

/// Log a message at the warn level.
template <detail::StringLiteral name = "kira", typename... Args>
inline void LogWarn(detail::FormatWithLocation fmt, Args &&...args) {
    GetLogger<name>()->log(
        detail::get_spdlog_source_loc(fmt.loc), spdlog::level::warn, fmt::runtime(fmt.fmt),
        std::forward<Args>(args)...
    );
}

/// Log a message at the error level.
template <detail::StringLiteral name = "kira", typename... Args>
inline void LogError(detail::FormatWithLocation fmt, Args &&...args) {
    GetLogger<name>()->log(
        detail::get_spdlog_source_loc(fmt.loc), spdlog::level::err, fmt::runtime(fmt.fmt),
        std::forward<Args>(args)...
    );
}
} // namespace kira
