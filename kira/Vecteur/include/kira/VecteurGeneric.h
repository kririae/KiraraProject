#pragma once

#include "kira/VecteurBase.h"
#include "kira/VecteurStorage.h"
#include "kira/VecteurTraits.h"
#include <type_traits>

namespace kira {
template <typename Scalar, std::size_t Size, typename Derived>
struct VecteurImpl : VecteurBase<Scalar, Size, Derived>,
                     VecteurStorage<Scalar, Size, alignof(Scalar)> {
private:
  using Base = VecteurBase<Scalar, Size, Derived>;
  using Storage = VecteurStorage<Scalar, Size, alignof(Scalar)>;

public:
  using Ref = std::add_lvalue_reference_t<Scalar>;
  using ConstRef = std::add_lvalue_reference_t<Scalar const>;

public:
  /// \name Constructors
  /// \{

  VecteurImpl() = default;

  /// Construct a vector with all elements set to `v`.
  constexpr VecteurImpl(Scalar const &v) {
    // Use a range-based for loop to initialize the elements.
    for (auto &x : *this)
      x = v;
  }

  /// Construct a vector with the elements set to `ts...`.
  template <typename... Ts>
  constexpr VecteurImpl(Ts... ts)
    requires((not Storage::is_dynamic()) and sizeof...(Ts) == Size)
      : Storage{ts...} {}

  /// Construct a vector with the elements from a span.
  constexpr VecteurImpl(std::span<Scalar const, Size> sp) {
    for (std::size_t i = 0; i < this->size(); ++i)
      this->entry(i) = sp[i];
  }

  /// Construct a vector with the elements from an array.
  ///
  /// \note This constructor is only available when the storage is static.
  constexpr VecteurImpl(std::array<Scalar const, Size> arr)
    requires(not Storage::is_dynamic())
  {
    for (std::size_t i = 0; i < this->size(); ++i)
      this->entry(i) = arr[i];
  }

  /// \}
public:
  /// \name Element access interface
  /// \{

  /// Fetch the i-th element of the vector in the storage.
  [[nodiscard]] constexpr ConstRef entry(std::size_t i) const {
    return *(this->data() + i);
  }

  /// \copydoc entry
  [[nodiscard]] constexpr Ref entry(std::size_t i) {
    return *(this->data() + i);
  }

  /// \}
public:
  /// \name Arithmetic interface
  /// \{
#define KIRA_ARITHMETIC_OPERATOR(name, op)                                     \
  template <is_vecteur RHS>                                                    \
  [[nodiscard]] constexpr auto name(RHS const &rhs) const {                    \
    typename PromotedType<Vecteur<Scalar, Size>, RHS>::result result;          \
    for (std::size_t i = 0; i < result.size(); ++i)                            \
      result.entry(i) = entry(i) op rhs.entry(i);                              \
    return result;                                                             \
  }                                                                            \
                                                                               \
  template <typename RHS>                                                      \
    requires(std::is_arithmetic_v<RHS>)                                        \
  [[nodiscard]] constexpr auto name(RHS const &rhs) const {                    \
    typename PromotedType<Vecteur<Scalar, Size>, RHS>::result result;          \
    for (std::size_t i = 0; i < result.size(); ++i)                            \
      result.entry(i) = entry(i) op rhs;                                       \
    return result;                                                             \
  }                                                                            \
                                                                               \
  template <typename LHS>                                                      \
    requires(std::is_arithmetic_v<LHS>)                                        \
  [[nodiscard]] constexpr auto r##name(LHS const &lhs) const {                 \
    typename PromotedType<LHS, Vecteur<Scalar, Size>>::result result;          \
    for (std::size_t i = 0; i < result.size(); ++i)                            \
      result.entry(i) = lhs op entry(i);                                       \
    return result;                                                             \
  }

  KIRA_ARITHMETIC_OPERATOR(add_, +);
  KIRA_ARITHMETIC_OPERATOR(sub_, -);
  KIRA_ARITHMETIC_OPERATOR(mul_, *);
  KIRA_ARITHMETIC_OPERATOR(div_, /);
  KIRA_ARITHMETIC_OPERATOR(mod_, %);
#undef KIRA_ARITHMETIC_OPERATOR
  /// \}
};

template <typename Scalar, std::size_t Size>
struct Vecteur : VecteurImpl<Scalar, Size, Vecteur<Scalar, Size>> {
  static_assert(std::is_arithmetic_v<Scalar>,
                "Scalar must be an arithmetic type (for now).");
  static_assert(Size > 0, "Size must be greater than 0.");

  using Base = VecteurImpl<Scalar, Size, Vecteur<Scalar, Size>>;
  using Base::Base;
};

template <typename... Ts>
Vecteur(Ts...) -> Vecteur<std::common_type_t<Ts...>, sizeof...(Ts)>;
} // namespace kira
