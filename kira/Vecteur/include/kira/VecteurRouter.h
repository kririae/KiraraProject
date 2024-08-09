#pragma once

#include "kira/VecteurTraits.h"

namespace kira {
/// \name Binary router
/// \{

//! NOTE(krr):
//! When pass by value, don't call the derived method.
//! When calling the function, call the derived method with constant evaluation context.

#define KIRA_ROUTE_BINARY(name, func)                                                              \
    template <is_vecteur LHS, is_vecteur RHS> constexpr auto name(const LHS &a1, const RHS &a2) {  \
        if (std::is_constant_evaluated())                                                          \
            return a1.template derived<true>().func(a2);                                           \
        return a1.template derived().func(a2);                                                     \
    }                                                                                              \
                                                                                                   \
    template <is_vecteur LHS, typename RHS>                                                        \
        requires(std::is_arithmetic_v<RHS>)                                                        \
    constexpr auto name(const LHS &a1, const RHS &a2) {                                            \
        if (std::is_constant_evaluated())                                                          \
            return a1.template derived<true>().func(a2);                                           \
        return a1.template derived().func(a2);                                                     \
    }                                                                                              \
                                                                                                   \
    template <typename LHS, is_vecteur RHS>                                                        \
        requires(std::is_arithmetic_v<LHS>)                                                        \
    constexpr auto name(const LHS &a1, const RHS &a2) {                                            \
        if (std::is_constant_evaluated())                                                          \
            return a2.template derived<true>().r##func(a1);                                        \
        return a2.template derived().r##func(a1);                                                  \
    }

KIRA_ROUTE_BINARY(operator+, add_);
KIRA_ROUTE_BINARY(operator-, sub_);
KIRA_ROUTE_BINARY(operator*, mul_);
KIRA_ROUTE_BINARY(operator/, div_);
KIRA_ROUTE_BINARY(operator%, mod_);
#undef KIRA_ROUTE_BINARY

constexpr auto operator==(is_vecteur auto const &a1, is_vecteur auto const &a2) {
    return a1.eq(a2);
}

constexpr auto operator-(is_vecteur auto const &a) { return a.neg(); }
/// \}
} // namespace kira
