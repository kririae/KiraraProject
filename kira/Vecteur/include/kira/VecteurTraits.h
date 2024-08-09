#pragma once

#include <span>
#include <type_traits>

#include "kira/VecteurBase.h"

namespace kira {
template <typename T> struct OperandType;
template <typename T> struct OperandType {
    using type = T;
    static constexpr std::size_t size = 1;
    static constexpr bool is_vecteur = false;
    static constexpr bool is_dynamic = false;
};

template <typename Scalar, std::size_t Size> struct OperandType<Vecteur<Scalar, Size>> {
    using type = Scalar;
    static constexpr std::size_t size = Size;
    static constexpr bool is_vecteur = true;
    static constexpr bool is_dynamic = (Size == std::dynamic_extent);
};

template <typename T>
concept is_vecteur = OperandType<T>::is_vecteur;

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
    static constexpr bool is_scalar = LHSType::is_scalar and RHSType::is_scalar;

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
template <typename From, typename To> struct is_safely_convertible_t {
    static constexpr bool value = is_safely_convertible<From, To>;
};
} // namespace kira
