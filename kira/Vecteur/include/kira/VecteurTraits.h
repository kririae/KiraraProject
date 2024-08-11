#pragma once

#include <span>
#include <type_traits>

#include "kira/VecteurBase.h"

namespace kira {
namespace detail {
struct no_assignment_operator { // NOLINT
    no_assignment_operator &operator=(no_assignment_operator const &) = delete;
};

template <typename T> struct ScalarOperandType {
    using type = T;
    static constexpr std::size_t size = 1;
    static constexpr bool is_vecteur = false;
    static constexpr bool is_dynamic = false;
    static constexpr bool is_leaf = false; // otherwise segfault
};

template <typename T> struct VecteurOperandType {
    using type = typename T::Scalar;
    static constexpr std::size_t size = T::Size;
    static constexpr bool is_vecteur = true;
    static constexpr bool is_dynamic = (size == std::dynamic_extent);
    static constexpr bool is_leaf = false;
};

template <typename Scalar, std::size_t Size, VecteurBackend backend>
struct VecteurOperandType<Vecteur<Scalar, Size, backend>> {
    using type = Scalar;
    static constexpr std::size_t size = Size;
    static constexpr bool is_vecteur = true;
    static constexpr bool is_dynamic = (size == std::dynamic_extent);
    static constexpr bool is_leaf = true;
};
} // namespace detail

// Any vecteur leaf that inherits from VectuerBase must be a vecteur.
template <typename T>
concept is_vecteur = std::is_base_of_v<VecteurBase<typename T::Scalar, T::Size, T>, T>;

template <typename T>
struct OperandType
    : std::conditional_t<
          is_vecteur<T>, detail::VecteurOperandType<T>, detail::ScalarOperandType<T>> {};

template <typename T>
concept is_leaf_vecteur = OperandType<T>::is_leaf;

template <typename T> struct VecteurLazyOperandType {
    using type = std::conditional_t<
        is_leaf_vecteur<T>, std::add_lvalue_reference_t<std::add_const_t<T>>, std::add_const_t<T>>;
};

template <typename LHS, typename RHS> struct PromotedType {
private:
    using LHSType = OperandType<LHS>;
    using RHSType = OperandType<RHS>;

public:
    using type = std::common_type_t<typename LHSType::type, typename RHSType::type>;
    static constexpr std::size_t size = []() consteval {
        // There are scalar/vecteur[s]/vecteur[d].
        // (1) scalar op scalar -> scalar
        // (2) scalar op vecteur[s] -> vecteur[s]
        // (3) scalar op vecteur[d] -> vecteur[d]
        // (4) vecteur[s] op scalar -> vecteur[s]
        // (5) vecteur[d] op scalar -> vecteur[d]
        // (6) vecteur[s] op vecteur[s] -> vecteur[s] (same size)
        // (7) vecteur[d] op vecteur[d] -> vecteur[d]
        // (8) vecteur[s] op vecteur[d] -> vecteur[d]
        // (9) vecteur[d] op vecteur[s] -> vecteur[d]
        if constexpr (LHSType::is_dynamic or RHSType::is_dynamic)
            // (3) (5) (7) (8) (9)
            return std::dynamic_extent;
        else if constexpr (LHSType::is_vecteur and RHSType::is_vecteur) {
            // (6)
            static_assert(
                LHSType::size == RHSType::size, "The size of the operands must be the same."
            );
            return LHSType::size;
        } else
            // (1) (2) (4)
            return std::max(LHSType::size, RHSType::size);
    }();

    /// The resulting vecteur type.
    using result = Vecteur<type, size>;
};

template <size_t LHSSize, size_t RHSSize>
concept is_static_operable =
    (LHSSize == RHSSize) || (LHSSize == std::dynamic_extent) || (RHSSize == std::dynamic_extent);

template <is_vecteur LHS, is_vecteur RHS>
constexpr auto CheckDynamicOperable(LHS const &lhs, RHS const &rhs) {
    if constexpr (LHS::is_dynamic() or RHS::is_dynamic()) {
        // You already use the dynamic vector, right?
        KIRA_FORCE_ASSERT(
            lhs.size() == rhs.size(), "The size of the operands must be the same: {} != {}",
            lhs.size(), rhs.size()
        );
    }
}

template <typename From, typename To>
concept is_safely_convertible =
    std::is_same_v<decltype(std::declval<From>() + std::declval<To>()), To>;

// For std::conjunction to work, we need to define a primary template.
// NOLINTNEXTLINE
template <typename From, typename To> struct is_safely_convertible_t {
    static constexpr bool value = is_safely_convertible<From, To>;
};

template <typename LHSScalar, typename RHSScalar> struct BinaryOpAdd {
    static constexpr std::string_view expr_str = "+";
    constexpr auto operator()(LHSScalar const &lhs, RHSScalar const &rhs) const
        -> PromotedType<LHSScalar, RHSScalar>::type {
        return lhs + rhs;
    }
};

template <typename LHSScalar, typename RHSScalar> struct BinaryOpSub {
    static constexpr std::string_view expr_str = "-";
    constexpr auto operator()(LHSScalar const &lhs, RHSScalar const &rhs) const
        -> PromotedType<LHSScalar, RHSScalar>::type {
        return lhs - rhs;
    }
};

template <typename LHSScalar, typename RHSScalar> struct BinaryOpMul {
    static constexpr std::string_view expr_str = "*";
    constexpr auto operator()(LHSScalar const &lhs, RHSScalar const &rhs) const
        -> PromotedType<LHSScalar, RHSScalar>::type {
        return lhs * rhs;
    }
};

template <typename LHSScalar, typename RHSScalar> struct BinaryOpDiv {
    static constexpr std::string_view expr_str = "/";
    constexpr auto operator()(LHSScalar const &lhs, RHSScalar const &rhs) const
        -> PromotedType<LHSScalar, RHSScalar>::type {
        return lhs / rhs;
    }
};

template <typename LHSScalar, typename RHSScalar> struct BinaryOpMod {
    static constexpr std::string_view expr_str = "%";
    constexpr auto operator()(LHSScalar const &lhs, RHSScalar const &rhs) const
        -> PromotedType<LHSScalar, RHSScalar>::type {
        return lhs % rhs;
    }
};

template <typename LHSScalar, typename RHSScalar> struct BinaryOpMax {
    static constexpr std::string_view expr_str = "max";
    constexpr auto operator()(LHSScalar const &lhs, RHSScalar const &rhs) const
        -> PromotedType<LHSScalar, RHSScalar>::type {
        return std::max(lhs, rhs);
    }
};

template <typename LHSScalar, typename RHSScalar> struct BinaryOpMin {
    static constexpr std::string_view expr_str = "min";
    constexpr auto operator()(LHSScalar const &lhs, RHSScalar const &rhs) const
        -> PromotedType<LHSScalar, RHSScalar>::type {
        return std::min(lhs, rhs);
    }
};

template <typename Scalar> struct UnaryOp0Abs {
    constexpr auto operator()(Scalar const &operand) const -> Scalar { return std::abs(operand); }
};

template <typename Scalar> struct UnaryOp0Ceil {
    constexpr auto operator()(Scalar const &operand) const -> Scalar { return std::ceil(operand); }
};

template <typename Scalar> struct UnaryOp0Exp {
    constexpr auto operator()(Scalar const &operand) const -> Scalar { return std::exp(operand); }
};

template <typename Scalar> struct UnaryOp0Floor {
    constexpr auto operator()(Scalar const &operand) const -> Scalar { return std::floor(operand); }
};

template <typename Scalar> struct UnaryOp0Log {
    constexpr auto operator()(Scalar const &operand) const -> Scalar { return std::log(operand); }
};

template <typename Scalar> struct UnaryOp0Round {
    constexpr auto operator()(Scalar const &operand) const -> Scalar { return std::round(operand); }
};

template <typename Scalar> struct UnaryOp0Sqrt {
    constexpr auto operator()(Scalar const &operand) const -> Scalar { return std::sqrt(operand); }
};

template <typename Scalar> struct UnaryOp0Neg {
    constexpr auto operator()(Scalar const &operand) const -> Scalar { return -operand; }
};

template <typename Scalar> struct UnaryOp0Sqr {
    constexpr auto operator()(Scalar const &operand) const -> Scalar { return operand * operand; }
};
} // namespace kira
