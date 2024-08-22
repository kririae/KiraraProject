#pragma once

#include <fmt/core.h>

#include "kira/Compiler.h"

namespace kira::detail {
template <typename Condition> // NOLINTNEXTLINE
constexpr auto __assert(auto cond_str, auto file, auto line, Condition &&cond) {
    if (KIRA_UNLIKELY(!(std::forward<Condition>(cond)))) {
        fmt::print(stderr, "Assertion ({:s}) failed at [{:s}:{:d}]\n", cond_str, file, line);
        std::fflush(stderr);
        std::abort();
    }
}

template <typename Condition, typename... Args> // NOLINTNEXTLINE
constexpr auto __assert(
    auto cond_str, auto file, auto line, Condition &&cond, fmt::format_string<Args...> fmt,
    Args &&...args
) {
    if (KIRA_UNLIKELY(!(std::forward<Condition>(cond)))) {
        fmt::print(
            stderr, "Assertion ({:s}) failed at [{:s}:{:d}]: {:s}\n", cond_str, file, line,
            fmt::format(fmt, std::forward<Args>(args)...)
        );
        std::fflush(stderr);
        std::abort();
    }
}

// NOLINTNEXTLINE
consteval std::string_view __filename(std::string_view sv) {
    auto pos = sv.rfind('/');
    if (pos == std::string_view::npos)
        pos = sv.rfind('\\');
    return (pos == std::string_view::npos) ? sv : sv.substr(pos + 1);
}
} // namespace kira::detail

#define __FILENAME__ ::kira::detail::__filename(__FILE__) /**/

/// \brief Macro to force an assertion even in release builds.
///
/// This macro will always check the condition and print an error message to stderr if the condition
/// is false.
#define KIRA_FORCE_ASSERT(cond, ...)                                                               \
    do {                                                                                           \
        constexpr auto filename = __FILENAME__;                                                    \
        ::kira::detail::__assert(#cond, filename, __LINE__, (cond)__VA_OPT__(, ) __VA_ARGS__);     \
    } while (false)

/// \brief Macro to perform an assertion in debug builds.
///
/// This macro will check the condition and print an error message to stderr if the condition is
/// false, but only in debug builds (i.e., when NDEBUG is not defined). In release builds, the
/// assertion is ignored.
#if !defined(NDEBUG)
#define KIRA_ASSERT(cond, ...) KIRA_FORCE_ASSERT(cond, __VA_ARGS__)
#else
#define KIRA_ASSERT(cond, ...)
#endif
