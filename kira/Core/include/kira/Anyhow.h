#pragma once

#include <exception>

#include "kira/Logger.h"

namespace kira {
/// A exception system that integrates with the logging system.
class Anyhow : public std::exception {
    // https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#e14-use-purpose-designed-user-defined-types-as-exceptions-not-built-in-types

public:
    /// \brief Construct an exception with default message.
    ///
    /// \example throw Anyhow{};
    Anyhow(std::source_location const &loc = std::source_location::current())
        : message("An error occurred") {
        GetLogger(defaultLoggerName.value)
            ->log(
                detail::get_spdlog_source_loc(loc), spdlog::level::err, "{:s}",
                std::string_view{message}
            );
        LogFlush();
    }

    /// \brief Construct an exception with a message.
    ///
    /// \example throw Anyhow("Something went wrong");
    /// \example throw Anyhow("Something went wrong: {}", 42);
    template <typename... Args>
    Anyhow(detail::FormatWithSourceLoc fmt, Args &&...args)
        : message(fmt::format(fmt::runtime(fmt.fmt), std::forward<Args>(args)...)) {
        GetLogger(defaultLoggerName.value)
            ->log(
                detail::get_spdlog_source_loc(fmt.loc), spdlog::level::err, "{:s}",
                std::string_view{message}
            );
        LogFlush();
    }

    /// Get the message associated with the exception.
    [[nodiscard]] char const *what() const noexcept override { return message.c_str(); }

private:
    std::string message;
};
} // namespace kira
