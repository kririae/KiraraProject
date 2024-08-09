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
struct VecteurImpl<Scalar, Size, true, Derived> : VecteurBase<Scalar, Size, Derived>,
                                                  VecteurStorage<Scalar, Size, alignof(Scalar)> {
private:
    using Base = VecteurBase<Scalar, Size, Derived>;
    using Storage = VecteurStorage<Scalar, Size, alignof(Scalar)>;

public:
    using Ref = std::add_lvalue_reference_t<Scalar>;
    using ConstRef = std::add_lvalue_reference_t<Scalar const>;
    using ConstexprImpl = VecteurImpl<Scalar, Size, true, Derived>;

public:
    /// Default constructor.
    VecteurImpl() = default;

    // -----------------------------------------------------------------------------------------------------------------
    /// \name Dynamic Constructors
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    /// Construct a vector with the elements from an initializer list.
    explicit VecteurImpl(std::initializer_list<Scalar> init)
        requires(Storage::is_dynamic())
        : Storage{init.size()} {
        std::copy_n(init.begin(), this->size(), this->begin());
    }

    /// Construct a vector with a given size.
    explicit constexpr VecteurImpl(std::size_t size)
        requires(Storage::is_dynamic())
        : Storage(size) {}

    /// Construct a vector with all elements set to `v`.
    explicit constexpr VecteurImpl(std::size_t size, Scalar const &v)
        requires(Storage::is_dynamic())
        : Storage(size) {
        std::fill_n(this->begin(), size, v);
    }

    /// Construct a vector with the elements from a span.
    template <typename RHS, size_t ASize>
    explicit constexpr VecteurImpl(std::span<RHS, ASize> sp)
        requires(Storage::is_dynamic() and is_safely_convertible<RHS, Scalar>)
        : Storage{sp.size()} {
        std::copy_n(sp.begin(), this->size(), this->begin());
    }

    /// Construct a vector with the elements from an array.
    template <typename RHS, size_t ASize>
    explicit constexpr VecteurImpl(std::array<RHS, ASize> const &arr)
        requires(Storage::is_dynamic() and is_safely_convertible<RHS, Scalar>)
        : Storage{arr.size()} {
        std::copy_n(arr.begin(), this->size(), this->begin());
    }

    /// Construct a vector with the elements from another vector.
    explicit constexpr VecteurImpl(VecteurImpl const &rhs)
        requires(Storage::is_dynamic())
        : Storage{rhs.size()} {
        std::copy_n(rhs.begin(), this->size(), this->begin());
    }

    /// Construct a vector by moving from another vector.
    explicit constexpr VecteurImpl(VecteurImpl &&rhs) noexcept
        requires(Storage::is_dynamic())
        : Storage{std::move(rhs)} {}

    /// Construct a vector with elements from another different-typed vector.
    template <is_vecteur RHS>
    explicit constexpr VecteurImpl(RHS const &rhs)
        requires(Storage::is_dynamic() and is_safely_convertible<typename RHS::Scalar, Scalar>)
        : Storage{rhs.size()} {
        std::copy_n(rhs.begin(), this->size(), this->begin());
    }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------

    // -----------------------------------------------------------------------------------------------------------------
    /// \name Static Constructors
    // -----------------------------------------------------------------------------------------------------------------
    /// \{
    /// Construct a vector with all elements set to `v`.
    explicit constexpr VecteurImpl(Scalar const &v)
        requires(not Storage::is_dynamic())
    {
        // Use a range-based for loop to initialize the elements.
        for (auto &x : *this)
            x = v;
    }

    /// Construct a vector with the elements set to `ts...`.
    template <typename... Ts>
    constexpr VecteurImpl(Ts... ts)
        requires((not Storage::is_dynamic()) and sizeof...(Ts) == Size)
        : Storage{static_cast<Scalar>(ts)...} {
        static_assert(
            std::conjunction_v<is_safely_convertible_t<Ts, Scalar>...>,
            "The types of the arguments must be safely convertible to the scalar type."
        );
    }

    /// Construct a vector with the elements from a span.
    template <typename RHS>
    explicit constexpr VecteurImpl(std::span<RHS, Size> sp)
        requires(not Storage::is_dynamic())
    {
        static_assert(
            is_safely_convertible<RHS, Scalar>,
            "The types must be safely convertible to the scalar."
        );
        for (std::size_t i = 0; i < this->size(); ++i)
            this->entry(i) = sp[i];
    }

    /// Construct a vector with the elements from an array.
    template <typename RHS>
    explicit constexpr VecteurImpl(std::array<RHS, Size> const &arr)
        requires(not Storage::is_dynamic())
    {
        static_assert(
            is_safely_convertible<RHS, Scalar>,
            "The types must be safely convertible to the scalar."
        );
        for (std::size_t i = 0; i < this->size(); ++i)
            this->entry(i) = arr[i];
    }

    /// Construct a vector with the elements from another vector.
    explicit constexpr VecteurImpl(VecteurImpl const &rhs)
        requires(not Storage::is_dynamic())
    {
        for (std::size_t i = 0; i < this->size(); ++i)
            this->entry(i) = rhs.entry(i);
    }

    /// Construct a vector with elements from another different-typed vector.
    template <is_vecteur RHS>
    explicit constexpr VecteurImpl(RHS const &rhs)
        requires(not Storage::is_dynamic() and Size == RHS::Size)
    {
        static_assert(
            is_safely_convertible<RHS, Scalar>,
            "The types must be safely convertible to the scalar."
        );

        // This implicitly assumes that RHS is a static vector.
        for (std::size_t i = 0; i < this->size(); ++i)
            this->entry(i) = rhs.entry(i);
    }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------
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

    /// \}
    // -----------------------------------------------------------------------------------------------------------------
public:
    //! NOTE(krr): the following implementations are not compatible with dynamic storage for
    //! now.

    // -----------------------------------------------------------------------------------------------------------------
    /// \name Binary arithmetic interface
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

#define KIRA_ARITHMETIC_OPERATOR(name, op)                                                         \
    template <is_vecteur RHS> [[nodiscard]] constexpr auto name(RHS const &rhs) const {            \
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
    [[nodiscard]] constexpr auto name(RHS const &rhs) const {                                      \
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
    [[nodiscard]] constexpr auto r##name(LHS const &lhs) const {                                   \
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
    /// \name Copy-assignment operators
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    //! NOTE(krr): Copy assignment matrix:
    //! | this \ rhs | static | dynamic | rvalue dynamic |
    //! |------------|--------|---------|----------------|
    //! | static     | Y(1)   | N(2)    | N(5)           |
    //! | dynamic    | Y(3)   | Y(4)    | Y(6)           |
    //! |------------|--------|---------|----------------|
    //!
    //! (1) statically checked
    //! (2) not safe
    //! (3) safe and dynamically checked, realloc if different size
    //! (4) safe and dynamically checked, realloc if different size
    //! (5) not safe
    //! (6) safe and dynamically checked, no realloc

    // (6) dynamic from rvalue dynamic
    constexpr decltype(auto) operator=(VecteurImpl &&rhs) noexcept
        requires(Storage::is_dynamic())
    {
        VecteurImpl tmp{std::move(rhs)};
        using std::swap;
        swap(static_cast<Storage &>(*this), static_cast<Storage &>(tmp));
        return *this;
    }

    // (4) dynamic from dynamic
    constexpr decltype(auto) operator=(VecteurImpl const &rhs)
        requires(Storage::is_dynamic())
    {
        if (this == &rhs)
            return *this;
        if (this->size() != rhs.size())
            this->realloc(rhs.size());
        for (std::size_t i = 0; i < this->size(); ++i)
            entry(i) = rhs.entry(i);
        return *this;
    }

    // (4') dyanmic from dynamic, different type
    template <is_vecteur RHS>
    constexpr decltype(auto) operator=(RHS const &rhs)
        requires(Storage::is_dynamic() and RHS::is_dynamic() and is_safely_convertible<typename RHS::Scalar, Scalar>)
    {
        static_assert(is_static_operable<Size, RHS::Size>);
        if (this->size() != rhs.size())
            this->realloc(rhs.size());
        for (std::size_t i = 0; i < this->size(); ++i)
            entry(i) = rhs.entry(i);
        return *this;
    }

    // (3) dynamic from static, all types
    template <is_vecteur RHS>
    constexpr decltype(auto) operator=(RHS const &rhs)
        requires(Storage::is_dynamic() and (not RHS::is_dynamic()) and is_safely_convertible<typename RHS::Scalar, Scalar>)
    {
        static_assert(not RHS::is_dynamic(), "The RHS must be a static vector.");
        if (this->size() != rhs.size())
            this->realloc(rhs.size());
        for (std::size_t i = 0; i < this->size(); ++i)
            entry(i) = rhs.entry(i);
        return *this;
    }

    // (1) static from static
    constexpr decltype(auto) operator=(VecteurImpl const &rhs)
        requires(not Storage::is_dynamic())
    {
        for (std::size_t i = 0; i < this->size(); ++i)
            entry(i) = rhs.entry(i);
        return *this;
    }

    // (1') static from static, different type
    template <is_vecteur RHS>
    constexpr decltype(auto) operator=(RHS const &rhs)
        requires((not Storage::is_dynamic) and (not RHS::is_dynamic) and is_safely_convertible<typename RHS::Scalar, Scalar>)
    {
        static_assert(
            is_static_operable<Size, RHS::Size>,
            "The scalar types of the operands must be the same."
        );
        for (std::size_t i = 0; i < this->size(); ++i)
            entry(i) = rhs.entry(i);
        return *this;
    }

    /// \}
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Binary comparable proxy
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    template <is_vecteur RHS>
    [[nodiscard]] constexpr auto max_(const RHS &rhs) const
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
    [[nodiscard]] constexpr auto max_(const RHS &rhs) const
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
    [[nodiscard]] constexpr auto min_(const RHS &rhs) const
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
    [[nodiscard]] constexpr auto min_(const RHS &rhs) const
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
    [[nodiscard]] constexpr auto dot_(RHS const &rhs) const
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
    [[nodiscard]] constexpr auto eq_(RHS const &rhs) const
        requires(is_static_operable<Size, RHS::Size>)
    {
        CheckDynamicOperable(this->derived(), rhs);
        bool result = entry(0_U) == rhs.entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result &= entry(i) == rhs.entry(i);
        return result;
    }

    template <is_vecteur RHS>
    [[nodiscard]] constexpr auto near_(RHS const &rhs, Scalar const &epsilon) const
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

    [[nodiscard]] constexpr auto hsum_() const {
        auto result = entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result += entry(i);
        return result;
    }

    [[nodiscard]] constexpr auto hprod_() const {
        auto result = entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result *= entry(i);
        return result;
    }

    [[nodiscard]] constexpr auto hmin_() const {
        auto result = entry(0_U);
        for (std::size_t i = 1; i < this->size(); ++i)
            result = std::min(result, entry(i));
        return result;
    }

    [[nodiscard]] constexpr auto hmax_() const {
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
    [[nodiscard]] constexpr auto name() const {                                                    \
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
struct VecteurImpl<Scalar, Size, false, Derived> : VecteurImpl<Scalar, Size, true, Derived> {
    using Base = VecteurImpl<Scalar, Size, true, Vecteur<Scalar, Size>>;
    using Base::Base;
    using Base::operator=;
};

template <typename Scalar, std::size_t Size>
struct Vecteur : VecteurImpl<Scalar, Size, false, Vecteur<Scalar, Size>> {
    static_assert(std::is_arithmetic_v<Scalar>, "Scalar must be an arithmetic type (for now).");
    static_assert(Size > 0, "Size must be greater than 0.");

    using Base = VecteurImpl<Scalar, Size, false, Vecteur<Scalar, Size>>;
    using Base::Base;
    using Base::operator=; // inherit to have better diagnostics.
};

template <typename... Ts> Vecteur(Ts...) -> Vecteur<std::common_type_t<Ts...>, sizeof...(Ts)>;
} // namespace kira
