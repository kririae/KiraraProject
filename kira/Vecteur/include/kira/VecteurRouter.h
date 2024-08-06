#pragma once

#include "kira/VecteurTraits.h"

namespace kira {
/// \name Binary router
/// \{
#define KIRA_ROUTE_BINARY(name, func)                                                              \
    template <is_vecteur LHS, is_vecteur RHS> constexpr auto name(const LHS &a1, const RHS &a2) {  \
        return a1.derived().func(a2.derived());                                                    \
    }                                                                                              \
                                                                                                   \
    template <is_vecteur LHS, typename RHS>                                                        \
        requires(std::is_arithmetic_v<RHS>)                                                        \
    constexpr auto name(const LHS &a1, const RHS &a2) {                                            \
        return a1.derived().func(a2);                                                              \
    }                                                                                              \
                                                                                                   \
    template <typename LHS, is_vecteur RHS>                                                        \
        requires(std::is_arithmetic_v<LHS>)                                                        \
    constexpr auto name(const LHS &a1, const RHS &a2) {                                            \
        return a2.derived().r##func(a1);                                                           \
    }

KIRA_ROUTE_BINARY(operator+, add_);
KIRA_ROUTE_BINARY(operator-, sub_);
KIRA_ROUTE_BINARY(operator*, mul_);
KIRA_ROUTE_BINARY(operator/, div_);
KIRA_ROUTE_BINARY(operator%, mod_);
#undef KIRA_ROUTE_BINARY
/// \}
} // namespace kira
