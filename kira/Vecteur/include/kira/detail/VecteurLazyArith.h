#pragma once

#include <string_view>

#include "kira/VecteurTraits.h"

namespace kira::detail {
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
} // namespace kira::detail
