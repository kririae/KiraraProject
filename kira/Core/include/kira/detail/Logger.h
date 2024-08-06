#pragma once

#include <spdlog/spdlog.h>

#include <filesystem>
#include <source_location>
#include <unordered_map>

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
    constexpr operator char const *() const { return value; }
    constexpr operator std::string_view() const { return value; }
};

/// Helper struct to construct the source location.
struct FormatWithSourceLoc {
    std::string_view fmt;
    std::source_location loc;

    /// https://stackoverflow.com/questions/57547273/how-to-use-source-location-in-a-variadic-template-function
    ///
    /// It will be tricky to enable both `source_location` and compile-time check
    /// in `fmt::print`. I'll stay with `fmt::runtime` for now.
    template <typename T>
        requires(std::is_convertible_v<T, std::string_view>)
    constexpr FormatWithSourceLoc(
        T fmt, std::source_location const &loc = std::source_location::current()
    )
        : fmt(std::move(fmt)), loc(loc) {}
};

/// The singleton to manage the sinks.
class SinkManager {
public:
    static SinkManager &GetInstance() {
        static SinkManager instance;
        return instance;
    }

    SinkManager() = default;
    ~SinkManager() = default;
    SinkManager(SinkManager const &) = delete;
    SinkManager &operator=(SinkManager const &) = delete;

public:
    /// Create or Get the console sink for the logger.
    spdlog::sink_ptr CreateConsoleSink();

    /// Create or Get the file sink for the logger.
    spdlog::sink_ptr CreateFileSink(std::filesystem::path const &path);

    /// Drop the console sink for the logger.
    ///
    /// \return Whether the sink is actually dropped.
    bool DropConsoleSink() noexcept;

    /// Drop the file sink for the logger.
    ///
    /// \return Whether the sink is actually dropped.
    bool DropFileSink(std::filesystem::path const &path) noexcept;

private:
    spdlog::sink_ptr consoleSink;
    std::unordered_map<std::filesystem::path, spdlog::sink_ptr> fileSinks;
};

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
///
/// \throw std::runtime_error If the logger could not be initialized.
/// \throw std::runtime_error If the logger is previously initiaized without being reseted through
/// `spdlog::drop*`.
std::shared_ptr<spdlog::logger>
CreateLogger(std::string_view name, bool console, std::optional<std::filesystem::path> const &path);
} // namespace kira::detail
