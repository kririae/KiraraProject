#pragma once

#include "kira/VecteurBase.h"
#include "kira/VecteurStorage.h"
#include "kira/VecteurTraits.h"

namespace kira {
//! NOTE(krr): As for pitfalls, we do suffer from the same dielmma as the Eigen library.
//! see https://eigen.tuxfamily.org/dox/TopicPitfalls.html
//!
//! For example, when evaluating the expression `auto d = a + b + c`, the expression tree will be
//! `d = CwiseBinaryOp(a, CwiseBinaryOp(b, c))`, which results in a temporary object being created
//! at `CwiseBinaryOp(b, c)`. I'm not that familar with the standard, but it seems like that the
//! extended lifetime extention rules apply to our case, i.e., recursive const-reference.
//! Anyway... let's stick to the must-work variant.

template <typename Scalar, std::size_t Size, typename Derived>
struct VecteurImpl<Scalar, Size, VecteurBackend::Lazy, false, Derived>
    : VecteurBase<Scalar, Size, Derived> {};

template <typename BinaryOp, typename LHS, typename RHS> struct CwiseBinaryOp;
template <typename UnaryOp, typename T> struct CwiseUnaryOp0;

template <typename Derived> struct VecteurLazyBase {
private:
    constexpr auto const &derived_() const { return *static_cast<Derived const *>(this); }
    constexpr auto &derived_() { return *static_cast<Derived *>(this); }

    template <template <typename...> typename BinaryOp, is_vecteur RHS>
    constexpr auto __make_binary_op_vv(RHS const &rhs) const { // NOLINT
        return CwiseBinaryOp<
            BinaryOp<typename Derived::Scalar, typename RHS::Scalar>, Derived, RHS>(
            derived_(), rhs.derived()
        );
    }

    template <template <typename...> typename BinaryOp, typename RHS>
    constexpr auto __make_binary_op_vs(RHS const &rhs) const { // NOLINT
        return CwiseBinaryOp<BinaryOp<typename Derived::Scalar, RHS>, Derived, RHS>(
            derived_(), rhs
        );
    }

    template <template <typename...> typename BinaryOp, typename LHS>
    constexpr auto __make_binary_op_sv(LHS const &lhs) const { // NOLINT
        return CwiseBinaryOp<BinaryOp<LHS, typename Derived::Scalar>, LHS, Derived>(
            lhs, derived_()
        );
    }

    template <template <typename...> typename UnaryOp0>
    constexpr auto __make_unary_op0() const { // NOLINT
        return CwiseUnaryOp0<UnaryOp0<typename Derived::Scalar>, Derived>(derived_());
    }

public:
    [[nodiscard]] static constexpr auto get_backend() { return VecteurBackend::Lazy; }

    constexpr auto eval_() const {
        auto const &node = derived_();

        // Create a non-initialized leaf node.
        auto result = Vecteur<typename Derived::Scalar, Derived::Size, VecteurBackend::Lazy>();
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

    KIRA_LAZY_ARITHMETIC_OP(add_, BinaryOpAdd)
    KIRA_LAZY_ARITHMETIC_OP(sub_, BinaryOpSub)
    KIRA_LAZY_ARITHMETIC_OP(mul_, BinaryOpMul)
    KIRA_LAZY_ARITHMETIC_OP(div_, BinaryOpDiv)
    KIRA_LAZY_ARITHMETIC_OP(mod_, BinaryOpMod)
#undef KIRA_LAZY_ARITHMETIC_OP

    template <is_vecteur RHS>
    constexpr auto max_(RHS const &rhs) const
        requires(is_static_operable<Derived::Size, RHS::Size>)
    {
        return __make_binary_op_vv<BinaryOpMax>(rhs);
    }

    template <typename RHS>
    constexpr auto max_(RHS const &rhs) const
        requires(std::is_arithmetic_v<RHS>)
    {
        return __make_binary_op_vs<BinaryOpMax>(rhs);
    }

    template <is_vecteur RHS>
    constexpr auto min_(RHS const &rhs) const
        requires(is_static_operable<Derived::Size, RHS::Size>)
    {
        return __make_binary_op_vv<BinaryOpMin>(rhs);
    }

    template <typename RHS>
    constexpr auto min_(RHS const &rhs) const
        requires(std::is_arithmetic_v<RHS>)
    {
        return __make_binary_op_vs<BinaryOpMin>(rhs);
    }

public:
    constexpr auto hsum_() const {
        auto result = derived_().entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result += derived_().entry(i);
        return result;
    }

    constexpr auto hprod_() const {
        auto result = derived_().entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result *= derived_().entry(i);
        return result;
    }

    constexpr auto hmin_() const {
        auto result = derived_().entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result = std::min(result, derived_().entry(i));
        return result;
    }

    constexpr auto hmax_() const {
        auto result = derived_().entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result = std::max(result, derived_().entry(i));
        return result;
    }

    constexpr auto abs_() const { return __make_unary_op0<UnaryOp0Abs>(); }
    constexpr auto ceil_() const { return __make_unary_op0<UnaryOp0Ceil>(); }
    constexpr auto exp_() const { return __make_unary_op0<UnaryOp0Exp>(); }
    constexpr auto floor_() const { return __make_unary_op0<UnaryOp0Floor>(); }
    constexpr auto log_() const { return __make_unary_op0<UnaryOp0Log>(); }
    constexpr auto round_() const { return __make_unary_op0<UnaryOp0Round>(); }
    constexpr auto sqrt_() const { return __make_unary_op0<UnaryOp0Sqrt>(); }
    constexpr auto neg_() const { return __make_unary_op0<UnaryOp0Neg>(); }
    constexpr auto sqr_() const { return __make_unary_op0<UnaryOp0Sqr>(); }
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
    constexpr CwiseBinaryOp(const LHS &lhs, const RHS &rhs) : lhs(lhs), rhs(rhs) {}

public:
    [[nodiscard]] constexpr auto entry(auto i) const {
        return BinaryOp{}(entry_or_scalar(lhs, i), entry_or_scalar(rhs, i));
    }

    [[nodiscard]] constexpr auto size() const {
        return std::max<size_t>(size_or_1(lhs), size_or_1(rhs));
    }

private:
    template <typename T> static constexpr auto entry_or_scalar(T const &obj, auto i) {
        if constexpr (is_vecteur<T>)
            return obj.entry(i);
        else
            return obj;
    }

    template <typename T> static constexpr auto size_or_1(T const &obj) {
        if constexpr (is_vecteur<T>)
            return obj.size();
        else
            return 1;
    }
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
    constexpr CwiseUnaryOp0(T const &operand) : operand(operand) {}

public:
    [[nodiscard]] constexpr auto entry(auto i) const { return UnaryOp0{}(operand.entry(i)); }
    [[nodiscard]] constexpr auto size() const { return operand.size(); }
};

#define KIRA_VECTEUR_LEAF_TYPE Vecteur<Scalar, Size, VecteurBackend::Lazy>
template <typename Scalar, std::size_t Size>
struct VecteurImpl<Scalar, Size, VecteurBackend::Lazy, false, KIRA_VECTEUR_LEAF_TYPE>
    : VecteurBase<Scalar, Size, KIRA_VECTEUR_LEAF_TYPE>,
      VecteurLazyBase<KIRA_VECTEUR_LEAF_TYPE>,
      VecteurStorage<Scalar, Size, alignof(Scalar)> {
private:
    using Base = VecteurBase<Scalar, Size, KIRA_VECTEUR_LEAF_TYPE>;
    using Storage = VecteurStorage<Scalar, Size, alignof(Scalar)>;
#undef KIRA_VECTEUR_LEAF_TYPE

public:
    using Ref = std::add_lvalue_reference_t<Scalar>;
    using ConstRef = std::add_lvalue_reference_t<Scalar const>;
    using ConstexprImpl = void;

    using Storage::Storage;
    using Storage::operator=;

public:
    [[nodiscard]] constexpr ConstRef entry(auto i) const { return *(this->data() + i); }
    [[nodiscard]] constexpr Ref entry(auto i) { return *(this->data() + i); }

public:
    template <is_vecteur RHS>
    constexpr VecteurImpl(RHS const &rhs)
        requires(RHS::is_lazy())
    {
        if constexpr (Storage::is_dynamic())
            this->realloc(rhs.size());
        for (size_t i = 0; i < rhs.size(); ++i)
            entry(i) = rhs.entry(i);
    }

    template <is_vecteur RHS>
    constexpr decltype(auto) operator=(RHS const &rhs)
        requires(RHS::is_lazy())
    {
        if constexpr (Storage::is_dynamic())
            this->realloc(rhs.size());
        for (size_t i = 0; i < rhs.size(); ++i)
            entry(i) = rhs.entry(i);
        return *this;
    }
};
} // namespace kira
