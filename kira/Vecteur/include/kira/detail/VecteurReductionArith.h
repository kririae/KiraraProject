#pragma once

#include "kira/VecteurTraits.h"

namespace kira::detail {
//! NOTE(krr): This is abstracted out, because many backends will have the same implementation,
//! while reduction is hard to be optimized by expression template.

template <typename Derived> struct VecteurReductionArithmeticBase {
private:
    constexpr auto const &derived_() const { return *static_cast<Derived const *>(this); }
    constexpr auto &derived_() { return *static_cast<Derived *>(this); }

public:
    template <is_vecteur RHS>
    constexpr auto dot_(RHS const &rhs) const
        requires(is_static_operable<Derived, RHS>)
    {
        CheckDynamicOperable(derived_(), rhs);
        auto result = derived_().entry(0_U) * rhs.entry(0_U);
        if (std::is_constant_evaluated())
            for (std::size_t i = 1; i < derived_().size(); ++i)
                result += derived_().entry(i) * rhs.entry(i);
        else
            for (std::size_t i = 1; i < derived_().size(); ++i)
                result = std::fma(derived_().entry(i), rhs.entry(i), result);
        return result;
    }

    template <is_vecteur RHS>
    constexpr auto eq_(RHS const &rhs) const
        requires(is_static_operable<Derived, RHS>)
    {
        CheckDynamicOperable(derived_(), rhs);
        bool result = derived_().entry(0_U) == rhs.entry(0_U);
        for (std::size_t i = 1; i < derived_().size(); ++i)
            result &= derived_().entry(i) == rhs.entry(i);
        return result;
    }

    template <is_vecteur RHS>
    constexpr auto near_(RHS const &rhs, auto const &epsilon) const
        requires(is_static_operable<Derived, RHS>)
    {
        CheckDynamicOperable(derived_(), rhs);

        auto sqrDist = typename Derived::Scalar{0};
        for (std::size_t i = 0; i < derived_().size(); ++i) {
            auto const diff = derived_().entry(i) - rhs.entry(i);
            sqrDist += diff * diff;
        }

        return sqrDist <= epsilon * epsilon;
    }

    constexpr auto norm2_() const { return derived_().sqr().hsum(); }
    constexpr auto norm_() const { return std::sqrt(norm2_()); }

    constexpr auto hsum_() const {
        auto result = derived_().entry(0_U);
        for (std::size_t i = 1; i < derived_().size(); ++i)
            result += derived_().entry(i);
        return result;
    }

    constexpr auto hprod_() const {
        auto result = derived_().entry(0_U);
        for (std::size_t i = 1; i < derived_().size(); ++i)
            result *= derived_().entry(i);
        return result;
    }

    constexpr auto hmin_() const {
        auto result = derived_().entry(0_U);
        for (std::size_t i = 1; i < derived_().size(); ++i)
            result = std::min(result, derived_().entry(i));
        return result;
    }

    constexpr auto hmax_() const {
        auto result = derived_().entry(0_U);
        for (std::size_t i = 1; i < derived_().size(); ++i)
            result = std::max(result, derived_().entry(i));
        return result;
    }
};
} // namespace kira::detail
