#pragma once

#include <fmt/core.h>

#include "kira/Compiler.h"

namespace kira {
/// Obtain the filename from the full path.
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

/// Force trigger an assertion with no explannation.
#define KIRA_FORCE_ASSERT1(cond)                                                                   \
    do {                                                                                           \
        if (KIRA_UNLIKELY(!(cond))) {                                                              \
            ::fmt::print(                                                                          \
                stderr, "Assertion ({:s}) failed in {:s}:{:d}\n", #cond, __FILENAME__, __LINE__    \
            );                                                                                     \
            ::fflush(stderr);                                                                      \
            ::std::abort();                                                                        \
        }                                                                                          \
    } while (false)

/// Force trigger an assertion with explannation.
#define KIRA_FORCE_ASSERT2(cond, explanation)                                                      \
    do {                                                                                           \
        if (KIRA_UNLIKELY(!(cond))) {                                                              \
            ::fmt::print(                                                                          \
                stderr, "Assertion ({:s}) failed in {:s}:{:d} (" explanation ")\n", #cond,         \
                __FILENAME__, __LINE__                                                             \
            );                                                                                     \
            ::fflush(stderr);                                                                      \
            ::std::abort();                                                                        \
        }                                                                                          \
    } while (false)
} // namespace kira
