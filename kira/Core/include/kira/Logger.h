#pragma once

#include <spdlog/common.h>

#include <optional>

#include "kira/Compiler.h"
#include "kira/detail/Logger.h"

namespace kira {
/// The logger name by default.
constexpr detail::StringLiteral defaultLoggerName = "kira";

/// The builder to create a logger.
class LoggerBuilder {
public:
    LoggerBuilder() noexcept : name(defaultLoggerName) {}
    LoggerBuilder(std::string_view name) noexcept : name(name) {}

    /// Set whether to log to console.
    [[nodiscard]] LoggerBuilder &to_console(bool inConsole) noexcept {
        this->console = inConsole;
        return *this;
    }

    /// Set the file path to log to.
    [[nodiscard]] LoggerBuilder &to_file(std::filesystem::path const &inPath) noexcept {
        this->path = inPath;
        return *this;
    }

    /// Set the filter level for the logger.
    [[nodiscard]] LoggerBuilder &filter_level(spdlog::level::level_enum const &inLevel) noexcept {
        this->level = inLevel;
        return *this;
    }

    /// Initializes the logger with the previously specified configuration.
    std::shared_ptr<spdlog::logger> init() const;

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

template <detail::StringLiteral name, spdlog::level::level_enum lvl>
struct LoggerCustomizationPoint {
    template <fmt::formattable<char>... Args>
    void operator()(detail::FormatWithSourceLoc fmt, Args &&...args) {
        GetLogger(name.value)
            ->log(
                detail::get_spdlog_source_loc(fmt.loc), lvl, fmt::runtime(fmt.fmt),
                std::forward<Args>(args)...
            );
    }
};

//! We follow a design like CPO, because other submodules might want to change the \c
//! defaultLoggerName. This makes it easier to create new \c Log.* function with other default
//! logger name. For example, with a single line
//! \code{.cpp}
//! inline kira::LoggerCustomizationPoint<"krd", spdlog::level::level_enum::trace> LogTrace;
//! \endcode
//! we can introduce the function to other scope.

// NOLINTBEGIN
/// Log a message at the trace level.
inline LoggerCustomizationPoint<defaultLoggerName, spdlog::level::level_enum::trace> LogTrace;

/// Log a message at the debug level.
inline LoggerCustomizationPoint<defaultLoggerName, spdlog::level::level_enum::debug> LogDebug;

/// Log a message at the info level.
inline LoggerCustomizationPoint<defaultLoggerName, spdlog::level::level_enum::info> LogInfo;

/// Log a message at the warn level.
inline LoggerCustomizationPoint<defaultLoggerName, spdlog::level::level_enum::warn> LogWarn;

/// Log a message at the error level.
inline LoggerCustomizationPoint<defaultLoggerName, spdlog::level::level_enum::err> LogError;
// NOLINTEND

/// Flush the logger.
template <detail::StringLiteral name = defaultLoggerName> void LogFlush() {
    GetLogger(name.value)->flush();
}
} // namespace kira
