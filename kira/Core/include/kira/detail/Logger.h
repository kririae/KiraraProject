#pragma once

#include <spdlog/spdlog.h>

#include <filesystem>
#include <source_location>

#include "kira/SmallVector.h"
#include "kira/Types.h"

namespace kira::detail {
/// Convert std::source_location to spdlog::source_loc.
[[nodiscard]] inline constexpr spdlog::source_loc
get_spdlog_source_loc(std::source_location const &loc) {
    return {loc.file_name(), static_cast<int32>(loc.line()), loc.function_name()};
}

/// Wrapper of the string in NTTP context.
template <size_t N> struct StringLiteral {
    char value[N];
    constexpr StringLiteral(char const (&str)[N]) { std::copy_n(str, N, value); }
};

/// Helper struct to construct the source location.
struct FormatWithLocation {
    std::string_view fmt;
    std::source_location loc;

    /// https://stackoverflow.com/questions/57547273/how-to-use-source-location-in-a-variadic-template-function
    ///
    /// It will be tricky to enable both `source_location` and compile-time check
    /// in `fmt::print`. I'll stay with `fmt::runtime` for now.
    template <typename T>
        requires(std::is_convertible_v<T, std::string_view>)
    constexpr FormatWithLocation(
        T fmt, std::source_location const &loc = std::source_location::current()
    )
        : fmt(std::move(fmt)), loc(loc) {}
};

/// Create the sinks for the logger.
///
/// \param console Whether to log to console
/// \param path Optional file path to log to
/// \return Vector of sinks
SmallVector<spdlog::sink_ptr> const &
CreateSinks(bool console, std::optional<std::filesystem::path> const &path);

/// \brief Create the logger that has not been previously created.
///
/// \param name The name of the logger, used in the filtering different
/// components.
/// \param console Whether to log to the console if not previously specified.
/// \param path Optional path to log to if not previously specified.
///
/// \return The shared pointer to the logger.
/// \note Even if the returned logger is discarded, the logger will be
/// remembered by spdlog itself thus can be obtained by spdlog::get.
/// \note If the logger has been previously created with the name, the behavior
/// is undefined.
///
/// \throw std::runtime_error If the logger could not be initialized.
std::shared_ptr<spdlog::logger>
CreateLogger(std::string_view name, bool console, std::optional<std::filesystem::path> const &path);
} // namespace kira::detail
