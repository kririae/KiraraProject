#pragma once

#include <exception>
#include <string_view>

#include "kira/Logger.h"

namespace kira {
/// A exception system that integrates with the logging system.
///
/// \remark Anyhow should not be used during logger initialization.
class Anyhow : public std::exception {
    // https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#e14-use-purpose-designed-user-defined-types-as-exceptions-not-built-in-types

public:
    /// \brief Construct an exception with default message.
    template <typename Boolean>
    Anyhow(
        Boolean const &logToConsole,
        std::source_location const &loc = std::source_location::current()
    )
        requires(std::is_same_v<Boolean, bool>)
    {
        if (logToConsole) {
            GetLogger(defaultLoggerName.value)
                ->log(
                    detail::get_spdlog_source_loc(loc), spdlog::level::err, "{:s}",
                    std::string_view{message}
                );
            LogFlush();
        }
    }

    /// \brief Construct an exception with a message.
    template <typename Boolean, typename... Args>
    Anyhow(Boolean const &logToConsole, detail::FormatWithSourceLoc fmt, Args &&...args)
        requires(std::is_same_v<Boolean, bool>)
        : message(fmt::format(fmt::runtime(fmt.fmt), std::forward<Args>(args)...)) {
        if (logToConsole) {
            GetLogger(defaultLoggerName.value)
                ->log(
                    detail::get_spdlog_source_loc(fmt.loc), spdlog::level::err, "{:s}",
                    std::string_view{message}
                );
            LogFlush();
        }
    }

    /// \brief Construct an exception with default message.
    ///
    /// \example throw Anyhow{};
    Anyhow(std::source_location const &loc = std::source_location::current()) : Anyhow(true, loc) {}

    /// \brief Construct an exception with a message.
    ///
    /// \example throw Anyhow("Something went wrong");
    /// \example throw Anyhow("Something went wrong: {}", 42);
    template <typename... Args>
    Anyhow(detail::FormatWithSourceLoc fmt, Args &&...args)
        : Anyhow(true, fmt, std::forward<Args>(args)...) {}

    /// Get the message associated with the exception.
    [[nodiscard]] char const *what() const noexcept override { return message.c_str(); }

public:
    using ReflectionType = std::string;
    explicit Anyhow(ReflectionType message) : message(std::move(message)) {}
    [[nodiscard]] ReflectionType reflection() const { return message; }

private:
    std::string message;
};
} // namespace kira
