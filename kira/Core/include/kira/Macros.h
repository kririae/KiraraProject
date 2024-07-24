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
/// \}

/// \brief Disallow the copy and assignment of a class.
///
/// This macro cooperates with the clang-tidy.
#define KIRA_DISALLOW_COPY_AND_ASSIGN(TypeName)                                \
  TypeName(const TypeName &) = delete;                                         \
  TypeName &operator=(const TypeName &) = delete;
} // namespace kira
