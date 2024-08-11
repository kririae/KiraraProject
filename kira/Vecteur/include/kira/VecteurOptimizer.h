#pragma once

#include "kira/Compiler.h"
#include "kira/VecteurBase.h"
#include "kira/VecteurTraits.h"
#include "kira/detail/VecteurLazyArith.h"
#include "kira/detail/VecteurUtils.h"

namespace kira {
template <typename BinaryOp, typename LHS, typename RHS> struct CwiseBinaryOp;
template <typename UnaryOp, typename T> struct CwiseUnaryOp0;
template <typename Derived> struct VecteurLazyBase;

//! These transform the expression template into a more calculation-friendly form.

/// T -> T
template <typename T> struct VecteurOptimizer {
    using type = T;

public:
    static constexpr bool is_success = false;
    typename VecteurLazyOperandType<T>::type t;

    explicit VecteurOptimizer(type const &t) : t(t) {}
    constexpr auto operator()() const { return t; }
};

#if 1
/// T0 * T1 + T2 -> FuseMultiplyAddOp
/// - (T0 * T1) -> I0
/// - (I0 + T2) -> FuseMultiplyAddOp
template <typename T0, typename T1, typename T2>
struct VecteurOptimizer<CwiseBinaryOp<
    detail::BinaryOpAdd<float, float>,                        // op
    CwiseBinaryOp<detail::BinaryOpMul<float, float>, T0, T1>, // (1)
    T2                                                        // (2)
    >> {
private:
    using I0 = CwiseBinaryOp<detail::BinaryOpMul<float, float>, T0, T1>;
    using type = struct FuseMultiplyAddOp
        : VecteurImpl<
              float, PromotedType<I0, T2>::size, VecteurBackend::Lazy, false, FuseMultiplyAddOp>,
          VecteurLazyBase<FuseMultiplyAddOp>, detail::no_assignment_operator {
        typename VecteurLazyOperandType<T0>::type t0;
        typename VecteurLazyOperandType<T1>::type t1;
        typename VecteurLazyOperandType<T2>::type t2;

    public:
        using ConstexprImpl = FuseMultiplyAddOp;
        constexpr FuseMultiplyAddOp(T0 const &t0, T1 const &t1, T2 const &t2)
            : t0(t0), t1(t1), t2(t2) {}

        KIRA_FORCEINLINE constexpr auto entry(auto i) const {
            return std::fmaf(
                detail::entry_or_scalar(t0, i), detail::entry_or_scalar(t1, i),
                detail::entry_or_scalar(t2, i)
            );
        }

        KIRA_FORCEINLINE constexpr auto size() const {
            return std::max<size_t>(
                {detail::size_or_1(t0), detail::size_or_1(t1), detail::size_or_1(t2)}
            );
        }
    };

public:
    type const t;
    static constexpr bool is_success = true;

    explicit VecteurOptimizer(is_vecteur auto const &t)
        : t(t.lhs_op().lhs_op(), t.lhs_op().rhs_op(), t.rhs_op()) {}
    constexpr auto operator()() const { return t; }
};
#endif
} // namespace kira
