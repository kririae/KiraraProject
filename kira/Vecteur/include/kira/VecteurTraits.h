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
concept is_vecteur =
    std::is_base_of_v<VecteurBase<typename T::Scalar, T::Size, T::get_backend(), T>, T>;

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

    static constexpr VecteurBackend backend = []() consteval {
        if constexpr (LHSType::is_vecteur and RHSType::is_vecteur)
            static_assert(LHS::get_backend() == RHS::get_backend(), "Incompatible backends.");

        if constexpr (LHSType::is_vecteur)
            return LHS::get_backend();
        else if constexpr (RHSType::is_vecteur)
            return RHS::get_backend();
        return VecteurBackend::Generic;
    }();

    /// The resulting vecteur type.
    using result = Vecteur<type, size, backend>;
};

template <typename LHS, typename RHS>
concept is_static_operable = ((LHS::Size == RHS::Size) || (LHS::Size == std::dynamic_extent) ||
                              (RHS::Size == std::dynamic_extent)) and
                             (LHS::get_backend() == RHS::get_backend());

template <is_vecteur LHS, is_vecteur RHS>
constexpr auto CheckDynamicOperable(LHS const &lhs, RHS const &rhs) {
    if constexpr (LHS::is_dynamic() or RHS::is_dynamic()) {
        // You already use the dynamic vector, right?
        KIRA_ASSERT(
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
} // namespace kira
