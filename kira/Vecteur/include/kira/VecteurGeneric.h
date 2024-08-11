#pragma once

#include <type_traits>

#include "kira/Assertions.h"
#include "kira/VecteurBase.h"
#include "kira/VecteurStorage.h"
#include "kira/VecteurTraits.h"
#include "kira/detail/VecteurReductionArith.h"

namespace kira {
namespace detail {
constexpr auto sqr(auto const &x) { return x * x; }
constexpr auto neg(auto const &x) { return -x; }
} // namespace detail

template <typename Scalar, std::size_t Size, typename Derived>
struct VecteurImpl<Scalar, Size, VecteurBackend::Generic, true, Derived>
    : VecteurBase<Scalar, Size, VecteurBackend::Generic, Derived>,
      VecteurStorage<Scalar, Size, alignof(Scalar)>,
      detail::VecteurReductionArithmeticBase<Derived> {
private:
    using Base = VecteurBase<Scalar, Size, VecteurBackend::Generic, Derived>;
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
        typename PromotedType<Vecteur<Scalar, Size, Base::get_backend()>, RHS>::result result;     \
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
        typename PromotedType<Vecteur<Scalar, Size, Base::get_backend()>, RHS>::result result;     \
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
        typename PromotedType<LHS, Vecteur<Scalar, Size, Base::get_backend()>>::result result;     \
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

    // krr: similar functions can be added here.
    // `normalize()` is a special case, where non-floating point types are not allowed.
    constexpr auto normalize_() const {
        auto result = Derived();
        if constexpr (Derived::is_dynamic())
            result.realloc(this->size());
        auto const norm = this->norm_();
        for (std::size_t i = 0; i < this->size(); ++i)
            result.entry(i) = entry(i) / norm;
        return result;
    }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Binary comparable proxy
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    template <is_vecteur RHS>
    constexpr auto max_(const RHS &rhs) const
        requires(is_static_operable<VecteurImpl, RHS>)
    {
        CheckDynamicOperable(this->derived(), rhs);
        typename PromotedType<Vecteur<Scalar, Size, Base::get_backend()>, RHS>::result result;
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
        typename PromotedType<Vecteur<Scalar, Size, Base::get_backend()>, RHS>::result result;
        if constexpr (decltype(result)::is_dynamic())
            result.realloc(this->size());
        for (std::size_t i = 0; i < this->size(); ++i)
            result.entry(i) = std::max(entry(i), rhs);
        return result;
    }

    template <is_vecteur RHS>
    constexpr auto min_(const RHS &rhs) const
        requires(is_static_operable<VecteurImpl, RHS>)
    {
        CheckDynamicOperable(this->derived(), rhs);
        typename PromotedType<Vecteur<Scalar, Size, Base::get_backend()>, RHS>::result result;
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
        typename PromotedType<Vecteur<Scalar, Size, Base::get_backend()>, RHS>::result result;
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
    /// \name Forall arithmetic proxy
    // -----------------------------------------------------------------------------------------------------------------
    /// \{
#define KIRA_FORALL_ARITHMETIC0(name, op)                                                          \
    constexpr auto name() const {                                                                  \
        auto result = Derived();                                                                   \
        if constexpr (Derived::is_dynamic())                                                       \
            result.realloc(this->size());                                                          \
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

template <typename Scalar, std::size_t Size, VecteurBackend backend>
struct Vecteur : VecteurImpl<Scalar, Size, backend, false, Vecteur<Scalar, Size, backend>> {
    static_assert(std::is_arithmetic_v<Scalar>, "Scalar must be an arithmetic type (for now).");
    static_assert(Size > 0, "Size must be greater than 0.");

    using Base = VecteurImpl<Scalar, Size, backend, false, Vecteur<Scalar, Size, backend>>;
    using Base::Base;
    using Base::operator=; // inherit to have better diagnostics.
};

/// Deduction guide for the Vecteur class.
template <typename... Ts>
Vecteur(Ts...) -> Vecteur<std::common_type_t<Ts...>, sizeof...(Ts), VecteurBackend::Generic>;
} // namespace kira
