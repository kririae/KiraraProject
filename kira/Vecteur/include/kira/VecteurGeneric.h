#pragma once

#include <type_traits>

#include "kira/Assertions.h"
#include "kira/VecteurBase.h"
#include "kira/VecteurStorage.h"
#include "kira/VecteurTraits.h"

namespace kira {
namespace detail {
constexpr auto sqr(auto const &x) { return x * x; }
constexpr auto neg(auto const &x) { return -x; }
} // namespace detail

template <typename Scalar, std::size_t Size, typename Derived>
struct VecteurImpl<Scalar, Size, VecteurBackend::Generic, true, Derived>
    : VecteurBase<Scalar, Size, Derived>, VecteurStorage<Scalar, Size, alignof(Scalar)> {
private:
    using Base = VecteurBase<Scalar, Size, Derived>;
    using Storage = VecteurStorage<Scalar, Size, alignof(Scalar)>;

public:
    using Ref = std::add_lvalue_reference_t<Scalar>;
    using ConstRef = std::add_lvalue_reference_t<Scalar const>;
    using ConstexprImpl = VecteurImpl<Scalar, Size, VecteurBackend::Generic, true, Derived>;

public:
    /// Default constructor.
    VecteurImpl() = default;
    using Storage::Storage;
    using Storage::operator=;

public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Element access interface
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    /// Fetch the i-th element of the vector in the storage.
    [[nodiscard]] constexpr ConstRef entry(auto i) const {
        KIRA_ASSERT(
            i < this->size(), "The index must be less than the size: {} < {}", size_t(i),
            this->size()
        );
        return *(this->data() + i);
    }

    /// \copydoc entry
    [[nodiscard]] constexpr Ref entry(auto i) {
        KIRA_ASSERT(
            i < this->size(), "The index must be less than the size: {} < {}", size_t(i),
            this->size()
        );
        return *(this->data() + i);
    }

    /// Evaluate the vector.
    constexpr auto const &eval_() const { return this->derived(); }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Binary arithmetic interface
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

#define KIRA_ARITHMETIC_OPERATOR(name, op)                                                         \
    template <is_vecteur RHS> constexpr auto name(RHS const &rhs) const {                          \
        CheckDynamicOperable(this->derived(), rhs);                                                \
        typename PromotedType<Vecteur<Scalar, Size>, RHS>::result result;                          \
        if constexpr (decltype(result)::is_dynamic())                                              \
            result.realloc(this->size());                                                          \
        for (std::size_t i = 0; i < this->size(); ++i)                                             \
            result.entry(i) = entry(i) op rhs.entry(i);                                            \
        return result;                                                                             \
    }                                                                                              \
                                                                                                   \
    template <typename RHS>                                                                        \
        requires(std::is_arithmetic_v<RHS>)                                                        \
    constexpr auto name(RHS const &rhs) const {                                                    \
        typename PromotedType<Vecteur<Scalar, Size>, RHS>::result result;                          \
        if constexpr (decltype(result)::is_dynamic())                                              \
            result.realloc(this->size());                                                          \
        for (std::size_t i = 0; i < this->size(); ++i)                                             \
            result.entry(i) = entry(i) op rhs;                                                     \
        return result;                                                                             \
    }                                                                                              \
                                                                                                   \
    template <typename LHS>                                                                        \
        requires(std::is_arithmetic_v<LHS>)                                                        \
    constexpr auto r##name(LHS const &lhs) const {                                                 \
        typename PromotedType<LHS, Vecteur<Scalar, Size>>::result result;                          \
        if constexpr (decltype(result)::is_dynamic())                                              \
            result.realloc(this->size());                                                          \
        for (std::size_t i = 0; i < this->size(); ++i)                                             \
            result.entry(i) = lhs op entry(i);                                                     \
        return result;                                                                             \
    }

    KIRA_ARITHMETIC_OPERATOR(add_, +);
    KIRA_ARITHMETIC_OPERATOR(sub_, -);
    KIRA_ARITHMETIC_OPERATOR(mul_, *);
    KIRA_ARITHMETIC_OPERATOR(div_, /);
    KIRA_ARITHMETIC_OPERATOR(mod_, %);
#undef KIRA_ARITHMETIC_OPERATOR
    /// \}
    // -----------------------------------------------------------------------------------------------------------------
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Binary comparable proxy
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    template <is_vecteur RHS>
    constexpr auto max_(const RHS &rhs) const
        requires(is_static_operable<Size, RHS::Size>)
    {
        CheckDynamicOperable(this->derived(), rhs);
        typename PromotedType<Vecteur<Scalar, Size>, RHS>::result result;
        if constexpr (decltype(result)::is_dynamic())
            result.realloc(this->size());
        for (std::size_t i = 0; i < this->size(); ++i)
            result.entry(i) = std::max(entry(i), rhs.entry(i));
        return result;
    }

    template <typename RHS>
    constexpr auto max_(const RHS &rhs) const
        requires(std::is_arithmetic_v<RHS>)
    {
        typename PromotedType<Vecteur<Scalar, Size>, RHS>::result result;
        if constexpr (decltype(result)::is_dynamic())
            result.realloc(this->size());
        for (std::size_t i = 0; i < this->size(); ++i)
            result.entry(i) = std::max(entry(i), rhs);
        return result;
    }

    template <is_vecteur RHS>
    constexpr auto min_(const RHS &rhs) const
        requires(is_static_operable<Size, RHS::Size>)
    {
        CheckDynamicOperable(this->derived(), rhs);
        typename PromotedType<Vecteur<Scalar, Size>, RHS>::result result;
        if constexpr (decltype(result)::is_dynamic())
            result.realloc(this->size());
        for (std::size_t i = 0; i < this->size(); ++i)
            result.entry(i) = std::min(entry(i), rhs.entry(i));
        return result;
    }

    template <typename RHS>
    constexpr auto min_(const RHS &rhs) const
        requires(std::is_arithmetic_v<RHS>)
    {
        typename PromotedType<Vecteur<Scalar, Size>, RHS>::result result;
        if constexpr (decltype(result)::is_dynamic())
            result.realloc(this->size());
        for (std::size_t i = 0; i < this->size(); ++i)
            result.entry(i) = std::min(entry(i), rhs);
        return result;
    }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Reduction arithmetic proxy
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    template <is_vecteur RHS>
    constexpr auto dot_(RHS const &rhs) const
        requires(is_static_operable<Size, RHS::Size>)
    {
        CheckDynamicOperable(this->derived(), rhs);
        auto result = entry(0_U) * rhs.entry(0_U);
        if (std::is_constant_evaluated())
            for (std::size_t i = 1; i < this->size(); ++i)
                result += entry(i) * rhs.entry(i);
        else
            for (std::size_t i = 1; i < this->size(); ++i)
                result = std::fma(entry(i), rhs.entry(i), result);
        return result;
    }

    template <is_vecteur RHS>
    constexpr auto eq_(RHS const &rhs) const
        requires(is_static_operable<Size, RHS::Size>)
    {
        CheckDynamicOperable(this->derived(), rhs);
        bool result = entry(0_U) == rhs.entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result &= entry(i) == rhs.entry(i);
        return result;
    }

    template <is_vecteur RHS>
    constexpr auto near_(RHS const &rhs, Scalar const &epsilon) const
        requires(is_static_operable<Size, RHS::Size>)
    {
        CheckDynamicOperable(this->derived(), rhs);

        auto sqrDist = Scalar{0};
        for (std::size_t i = 0; i < this->size(); ++i) {
            auto const diff = entry(i) - rhs.entry(i);
            sqrDist += diff * diff;
        }

        return sqrDist <= epsilon * epsilon;
    }

    constexpr auto hsum_() const {
        auto result = entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result += entry(i);
        return result;
    }

    constexpr auto hprod_() const {
        auto result = entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result *= entry(i);
        return result;
    }

    constexpr auto hmin_() const {
        auto result = entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result = std::min(result, entry(i));
        return result;
    }

    constexpr auto hmax_() const {
        auto result = entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result = std::max(result, entry(i));
        return result;
    }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Forall arithmetic proxy
    // -----------------------------------------------------------------------------------------------------------------
    /// \{
#define KIRA_FORALL_ARITHMETIC0(name, op)                                                          \
    constexpr auto name() const {                                                                  \
        auto result = Derived();                                                                   \
        for (std::size_t i = 0; i < this->size(); ++i)                                             \
            result.entry(i) = op(entry(i));                                                        \
        return result;                                                                             \
    }

    KIRA_FORALL_ARITHMETIC0(abs_, std::abs)     /// @0
    KIRA_FORALL_ARITHMETIC0(ceil_, std::ceil)   /// @1
    KIRA_FORALL_ARITHMETIC0(exp_, std::exp)     /// @2
    KIRA_FORALL_ARITHMETIC0(floor_, std::floor) /// @3
    KIRA_FORALL_ARITHMETIC0(log_, std::log)     /// @4
    KIRA_FORALL_ARITHMETIC0(round_, std::round) /// @5
    KIRA_FORALL_ARITHMETIC0(sqrt_, std::sqrt)   /// @6

    KIRA_FORALL_ARITHMETIC0(neg_, detail::neg) /// @7
    KIRA_FORALL_ARITHMETIC0(sqr_, detail::sqr) /// @8

    // krr: similar functions can be added here.
#undef KIRA_FORALL_ARITHMETIC
    /// \}
    // -----------------------------------------------------------------------------------------------------------------
};

/// A Fake a non-constexpr base when all backends are not presented.
template <typename Scalar, std::size_t Size, typename Derived>
struct VecteurImpl<Scalar, Size, VecteurBackend::Generic, false, Derived>
    : VecteurImpl<Scalar, Size, VecteurBackend::Generic, true, Derived> {
    using Base = VecteurImpl<Scalar, Size, VecteurBackend::Generic, true, Vecteur<Scalar, Size>>;
    using Base::Base;
    using Base::operator=;
};

template <typename Scalar, std::size_t Size, VecteurBackend backend>
struct Vecteur : VecteurImpl<Scalar, Size, backend, false, Vecteur<Scalar, Size, backend>> {
    static_assert(std::is_arithmetic_v<Scalar>, "Scalar must be an arithmetic type (for now).");
    static_assert(Size > 0, "Size must be greater than 0.");

    using Base = VecteurImpl<Scalar, Size, backend, false, Vecteur<Scalar, Size, backend>>;
    using Base::Base;
    using Base::operator=; // inherit to have better diagnostics.

public:
    /// Get the backend of the vector.
    ///
    /// \remark Defined for each leaf node.
    [[nodiscard]] static constexpr auto get_backend() { return backend; }
};

/// Deduction guide for the Vecteur class.
template <typename... Ts> Vecteur(Ts...) -> Vecteur<std::common_type_t<Ts...>, sizeof...(Ts)>;
} // namespace kira
