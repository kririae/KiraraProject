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
} // namespace kira::detail

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define KIRA_FORCE_ASSERT(cond, ...)                                                               \
    ::kira::detail::__assert(#cond, __FILENAME__, __LINE__, (cond)__VA_OPT__(, ) __VA_ARGS__) /**/

#if !defined(NDEBUG)
#define KIRA_ASSERT(cond, ...) KIRA_FORCE_ASSERT(cond, __VA_ARGS__)
#else
#define KIRA_ASSERT(cond, ...)
#endif
