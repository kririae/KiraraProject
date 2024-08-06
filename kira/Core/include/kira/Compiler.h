#pragma once

#include <fmt/core.h>

namespace kira {
/// \brief Cherry-picked useful macros from LLVM and some elsewhere.
/// \{
#ifndef __has_feature
#define __has_feature(x) 0
#endif

#ifndef __has_extension
#define __has_extension(x) 0
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#ifndef __has_include
#define __has_include(x) 0
#endif

#ifndef KIRA_HAS_CPP_ATTRIBUTE
#if defined(__cplusplus) && defined(__has_cpp_attribute)
#define KIRA_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define KIRA_HAS_CPP_ATTRIBUTE(x) 0
#endif
#endif

#if __has_builtin(__builtin_expect) || defined(__GNUC__)
#define KIRA_LIKELY(EXPR)   __builtin_expect((bool)(EXPR), true)
#define KIRA_UNLIKELY(EXPR) __builtin_expect((bool)(EXPR), false)
#else
#define KIRA_LIKELY(EXPR)   (EXPR)
#define KIRA_UNLIKELY(EXPR) (EXPR)
#endif

/// Apply this to owning classes like SmallVector to enable lifetime warnings.
#if KIRA_HAS_CPP_ATTRIBUTE(gsl::Owner)
#define KIRA_GSL_OWNER [[gsl::Owner]]
#else
#define KIRA_GSL_OWNER
#endif

/// Annotate the variable with the lifetime bound attribute.
/// see https://clang.llvm.org/docs/AttributeReference.html#lifetimebound
#if KIRA_HAS_CPP_ATTRIBUTE(clang::lifetimebound)
#define KIRA_LIFETIME_BOUND [[clang::lifetimebound]]
#elif KIRA_HAS_CPP_ATTRIBUTE(lifetimebound)
#define KIRA_LIKIRA_LIFETIME_BOUND [[lifetimebound]]
#elif KIRA_HAS_CPP_ATTRIBUTE(msvc::lifetimebound)
#define KIRA_LIFEKIRA_LIFETIME_BOUND [[msvc::lifetimebound]]
#else
#define KIRAKIRA_LIFETIME_BOUND
#endif

#if defined(__clang__) || defined(__GNUC__)
#define KIRA_DIAGNOSTIC_PUSH         _Pragma("GCC diagnostic push")
#define KIRA_IGNORE_UNUSED_PARAMETER _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"")
#define KIRA_DIAGNOSTIC_POP          _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#define KIRA_DIAGNOSTIC_PUSH
#define KIRA_IGNORE_UNUSED_PARAMETER
#define KIRA_DIAGNOSTIC_POP
#endif
/// \}
} // namespace kira
