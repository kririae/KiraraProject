#pragma once

#include <exception>
#include <string_view>

#include "kira/Logger.h"

namespace kira {
/// An exception system that integrates with the logging system.
///
/// \remark Anyhow should not be used during logger initialization.
/// \remark Generally, one should not call with \c logToConsole set to \c true, as the exception
/// might be rethrown.
class Anyhow final : public std::exception {
public:
    /// Construct an exception with default message.
    template <typename Boolean>
    explicit Anyhow(
        Boolean const &logToConsole,
        std::source_location const &loc = std::source_location::current()
    )
        requires(std::is_same_v<Boolean, bool>)
        : message("An error occurred"), sourceLoc(loc) {
        if (logToConsole)
            emit();
    }

    /// Construct an exception with a message.
    template <typename Boolean, typename... Args>
    Anyhow(Boolean const &logToConsole, detail::FormatWithSourceLoc fmt, Args &&...args)
        requires(std::is_same_v<Boolean, bool>)
        : message(fmt::format(fmt::runtime(fmt.fmt), std::forward<Args>(args)...)),
          sourceLoc(fmt.loc) {
        if (logToConsole)
            emit();
    }

    /// Construct an exception with default message.
    ///
    /// Example: \code{.cpp} throw Anyhow{}; \endcode
    explicit Anyhow(std::source_location const &loc = std::source_location::current())
        : Anyhow(false, loc) {}

    /// Construct an exception with a message.
    ///
    /// Example: \code{.cpp}
    /// throw Anyhow("Something went wrong");
    /// throw Anyhow("Something went wrong: {}", 42);
    /// \endcode
    template <typename... Args>
    explicit Anyhow(detail::FormatWithSourceLoc fmt, Args &&...args)
        : Anyhow(false, fmt, std::forward<Args>(args)...) {}

    /// Get the message associated with the exception.
    [[nodiscard]] char const *what() const noexcept override { return message.c_str(); }

    /// Emit the exception to the logging system.
    ///
    /// \param loggerName The name of the logger to use.
    /// \remark The default logger name is used if not specified.
    void emit(std::string_view loggerName = defaultLoggerName.value) const {
        GetLogger(std::string{loggerName})
            ->log(
                detail::get_spdlog_source_loc(sourceLoc), spdlog::level::err, "{:s}",
                std::string_view{message}
            );
        LogFlush();
    }

private:
    /// The message associated with the exception.
    std::string message;

    /// The source location where the exception was thrown.
    std::source_location sourceLoc;
};
} // namespace kira
