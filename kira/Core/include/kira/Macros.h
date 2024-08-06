#pragma once

namespace kira {
/// \brief Disallow the copy and assignment of a class.
///
/// This macro cooperates with the clang-tidy.
#define KIRA_DISALLOW_COPY_AND_ASSIGN(TypeName)                                                    \
    TypeName(const TypeName &) = delete;                                                           \
    TypeName &operator=(const TypeName &) = delete;
} // namespace kira
