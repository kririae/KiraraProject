#pragma once

namespace kira {
/// \brief Overload pattern for `std::visit`.
///
/// This struct template allows multiple callable objects to be combined
/// into a single object, which can then be used with `std::visit`.
///
/// \example ```cpp
///   std::visit(
///     kira::Overload{
///       [](const variant_type1& c) { /* ... */ },
///       [](const variant_type2& c) { /* ... */ },
///       [](const variant_type3& c) { /* ... */ }
///     },
///     variant_object
///   );
/// ```
template <class... Ts> struct Overload : Ts... {
  using Ts::operator()...;
};

/// Deduction guide for `kira::overload`.
template <class... Ts> Overload(Ts...) -> Overload<Ts...>;
} // namespace kira