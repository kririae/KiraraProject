#pragma once

#include "kira/Compiler.h"
#include "kira/VecteurBase.h"
#include "kira/VecteurOptimizer.h"
#include "kira/VecteurStorage.h"
#include "kira/VecteurTraits.h"
#include "kira/detail/VecteurLazyArith.h"
#include "kira/detail/VecteurReductionArith.h"
#include "kira/detail/VecteurUtils.h"

namespace kira {
//! NOTE(krr): As for pitfalls, we do suffer from the same dielmma as the Eigen library.
//! see https://eigen.tuxfamily.org/dox/TopicPitfalls.html
//!
//! For example, when evaluating the expression `auto d = a + b + c`, the expression tree will be
//! `d = CwiseBinaryOp(a, CwiseBinaryOp(b, c))`, which results in a temporary object being created
//! at `CwiseBinaryOp(b, c)`.

template <typename Scalar, std::size_t Size, typename Derived>
struct VecteurImpl<Scalar, Size, VecteurBackend::Lazy, false, Derived>
    : VecteurBase<Scalar, Size, VecteurBackend::Lazy, Derived> {};

template <typename BinaryOp, typename LHS, typename RHS> struct CwiseBinaryOp;
template <typename UnaryOp, typename T> struct CwiseUnaryOp0;

template <typename Derived>
struct VecteurLazyBase : detail::VecteurReductionArithmeticBase<Derived> {
private:
    constexpr auto const &derived_() const { return *static_cast<Derived const *>(this); }
    constexpr auto &derived_() { return *static_cast<Derived *>(this); }

    template <template <typename...> typename BinaryOp, is_vecteur RHS>
    constexpr auto __make_binary_op_vv(RHS const &rhs) const { // NOLINT
        return VecteurOptimizer{
            CwiseBinaryOp<BinaryOp<typename Derived::Scalar, typename RHS::Scalar>, Derived, RHS>(
                derived_(), rhs.derived()
            )
        }();
    }

    template <template <typename...> typename BinaryOp, typename RHS>
    constexpr auto __make_binary_op_vs(RHS const &rhs) const { // NOLINT
        return VecteurOptimizer{
            CwiseBinaryOp<BinaryOp<typename Derived::Scalar, RHS>, Derived, RHS>(derived_(), rhs)
        }();
    }

    template <template <typename...> typename BinaryOp, typename LHS>
    constexpr auto __make_binary_op_sv(LHS const &lhs) const { // NOLINT
        return VecteurOptimizer{
            CwiseBinaryOp<BinaryOp<LHS, typename Derived::Scalar>, LHS, Derived>(lhs, derived_())
        }();
    }

    template <template <typename...> typename UnaryOp0>
    constexpr auto __make_unary_op0() const { // NOLINT
        return CwiseUnaryOp0<UnaryOp0<typename Derived::Scalar>, Derived>(derived_());
    }

public:
    constexpr auto eval_() const {
        auto const &node = derived_();

        // Create a non-initialized leaf node.
        auto result = Vecteur<typename Derived::Scalar, Derived::Size, VecteurBackend::Lazy>();
        if constexpr (decltype(result)::is_dynamic())
            result.realloc(node.size());
        for (size_t i = 0; i < node.size(); ++i)
            result.entry(i) = node.entry(i);
        return result;
    }

public:
#define KIRA_LAZY_ARITHMETIC_OP(name, op)                                                          \
    template <is_vecteur RHS> constexpr auto name(RHS const &rhs) const {                          \
        return __make_binary_op_vv<op>(rhs);                                                       \
    }                                                                                              \
                                                                                                   \
    template <typename RHS> constexpr auto name(RHS const &rhs) const {                            \
        return __make_binary_op_vs<op>(rhs);                                                       \
    }                                                                                              \
                                                                                                   \
    template <typename LHS> constexpr auto r##name(LHS const &lhs) const {                         \
        return __make_binary_op_sv<op>(lhs);                                                       \
    }

    KIRA_LAZY_ARITHMETIC_OP(add_, detail::BinaryOpAdd)
    KIRA_LAZY_ARITHMETIC_OP(sub_, detail::BinaryOpSub)
    KIRA_LAZY_ARITHMETIC_OP(mul_, detail::BinaryOpMul)
    KIRA_LAZY_ARITHMETIC_OP(div_, detail::BinaryOpDiv)
    KIRA_LAZY_ARITHMETIC_OP(mod_, detail::BinaryOpMod)
#undef KIRA_LAZY_ARITHMETIC_OP

    constexpr auto normalize_() const {
        // No avoid division by zero.
        return __make_binary_op_vs<detail::BinaryOpDiv>(derived_().norm());
    }

    template <is_vecteur RHS>
    constexpr auto max_(RHS const &rhs) const
        requires(is_static_operable<Derived, RHS>)
    {
        return __make_binary_op_vv<detail::BinaryOpMax>(rhs);
    }

    template <typename RHS>
    constexpr auto max_(RHS const &rhs) const
        requires(std::is_arithmetic_v<RHS>)
    {
        return __make_binary_op_vs<detail::BinaryOpMax>(rhs);
    }

    template <is_vecteur RHS>
    constexpr auto min_(RHS const &rhs) const
        requires(is_static_operable<Derived, RHS>)
    {
        return __make_binary_op_vv<detail::BinaryOpMin>(rhs);
    }

    template <typename RHS>
    constexpr auto min_(RHS const &rhs) const
        requires(std::is_arithmetic_v<RHS>)
    {
        return __make_binary_op_vs<detail::BinaryOpMin>(rhs);
    }

public:
    constexpr auto abs_() const { return __make_unary_op0<detail::UnaryOp0Abs>(); }
    constexpr auto ceil_() const { return __make_unary_op0<detail::UnaryOp0Ceil>(); }
    constexpr auto exp_() const { return __make_unary_op0<detail::UnaryOp0Exp>(); }
    constexpr auto floor_() const { return __make_unary_op0<detail::UnaryOp0Floor>(); }
    constexpr auto log_() const { return __make_unary_op0<detail::UnaryOp0Log>(); }
    constexpr auto round_() const { return __make_unary_op0<detail::UnaryOp0Round>(); }
    constexpr auto sqrt_() const { return __make_unary_op0<detail::UnaryOp0Sqrt>(); }
    constexpr auto neg_() const { return __make_unary_op0<detail::UnaryOp0Neg>(); }
    constexpr auto sqr_() const { return __make_unary_op0<detail::UnaryOp0Sqr>(); }
};

template <typename BinaryOp, typename LHS, typename RHS>
struct CwiseBinaryOp : VecteurImpl<
                           typename PromotedType<LHS, RHS>::type, PromotedType<LHS, RHS>::size,
                           VecteurBackend::Lazy, false, CwiseBinaryOp<BinaryOp, LHS, RHS>>,
                       VecteurLazyBase<CwiseBinaryOp<BinaryOp, LHS, RHS>>,
                       detail::no_assignment_operator {
private:
    typename VecteurLazyOperandType<LHS>::type lhs;
    typename VecteurLazyOperandType<RHS>::type rhs;

public:
    using ConstexprImpl = CwiseBinaryOp;
    constexpr CwiseBinaryOp(const LHS &lhs, const RHS &rhs) : lhs(lhs), rhs(rhs) {
        if constexpr (is_vecteur<LHS> and is_vecteur<RHS>) {
            static_assert(is_static_operable<LHS, RHS>, "Incompatible sizes.");
            CheckDynamicOperable(lhs, rhs);
        }
    }

public:
    [[nodiscard]] KIRA_FORCEINLINE constexpr auto entry(auto i) const {
        return BinaryOp{}(detail::entry_or_scalar(lhs, i), detail::entry_or_scalar(rhs, i));
    }

    [[nodiscard]] KIRA_FORCEINLINE constexpr auto size() const {
        // Different sized vecteur are checked at construction time.
        return std::max<size_t>(detail::size_or_1(lhs), detail::size_or_1(rhs));
    }

    constexpr decltype(auto) lhs_op() const { return lhs; }
    constexpr decltype(auto) rhs_op() const { return rhs; }
};

template <typename UnaryOp0, typename T>
struct CwiseUnaryOp0
    : VecteurImpl<
          typename T::Scalar, T::Size, VecteurBackend::Lazy, false, CwiseUnaryOp0<UnaryOp0, T>>,
      VecteurLazyBase<CwiseUnaryOp0<UnaryOp0, T>>,
      detail::no_assignment_operator {
private:
    typename VecteurLazyOperandType<T>::type operand;

public:
    using ConstexprImpl = CwiseUnaryOp0;
    constexpr CwiseUnaryOp0(T const &operand) : operand(operand) {}

public:
    [[nodiscard]] KIRA_FORCEINLINE constexpr auto entry(auto i) const {
        return UnaryOp0{}(operand.entry(i));
    }

    [[nodiscard]] KIRA_FORCEINLINE constexpr auto size() const { return operand.size(); }
};

#define KIRA_VECTEUR_LEAF_TYPE Vecteur<Scalar, Size, VecteurBackend::Lazy>
template <typename Scalar, std::size_t Size>
struct VecteurImpl<Scalar, Size, VecteurBackend::Lazy, false, KIRA_VECTEUR_LEAF_TYPE>
    : VecteurBase<Scalar, Size, VecteurBackend::Lazy, KIRA_VECTEUR_LEAF_TYPE>,
      VecteurLazyBase<KIRA_VECTEUR_LEAF_TYPE>,
      VecteurStorage<Scalar, Size, alignof(Scalar)> {
private:
    using Base = VecteurBase<Scalar, Size, VecteurBackend::Lazy, KIRA_VECTEUR_LEAF_TYPE>;
    using Storage = VecteurStorage<Scalar, Size, alignof(Scalar)>;
#undef KIRA_VECTEUR_LEAF_TYPE

public:
    using Ref = std::add_lvalue_reference_t<Scalar>;
    using ConstRef = std::add_lvalue_reference_t<Scalar const>;
    using ConstexprImpl = VecteurImpl;

    using Storage::Storage;
    using Storage::operator=;

public:
    [[nodiscard]] constexpr ConstRef entry(auto i) const {
        KIRA_ASSERT(i < this->size(), "Index out of bounds: {} < {}", i, this->size());
        return *(this->data() + i);
    }

    [[nodiscard]] constexpr Ref entry(auto i) {
        KIRA_ASSERT(i < this->size(), "Index out of bounds: {} < {}", i, this->size());
        return *(this->data() + i);
    }

public:
    template <is_vecteur RHS>
    constexpr VecteurImpl(RHS const &rhs)
        requires(RHS::is_lazy())
    {
        if constexpr (Base::is_dynamic())
            this->realloc(rhs.size());
        for (size_t i = 0; i < rhs.size(); ++i)
            entry(i) = rhs.entry(i);
    }

    template <is_vecteur RHS>
    constexpr decltype(auto) operator=(RHS const &rhs)
        requires(RHS::is_lazy())
    {
        if constexpr (Base::is_dynamic())
            this->realloc(rhs.size());
        for (size_t i = 0; i < rhs.size(); ++i)
            entry(i) = rhs.entry(i);
        return *this;
    }
};
} // namespace kira
