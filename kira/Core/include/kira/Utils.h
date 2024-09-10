#pragma once

namespace kira {
/// \brief Overload pattern for \c std::visit.
///
/// This struct template allows multiple callable objects to be combined
/// into a single object, which can then be used with \c std::visit.
///
/// Example: \code{.cpp}
/// std::visit(
///     kira::Overload{
///         [](const variant_type1& c) { /* ... */ },
///         [](const variant_type2& c) { /* ... */ },
///         [](const variant_type3& c) { /* ... */ }
///     },
///     variant_object
/// );
/// \endcode
template <class... Ts> struct Overload : Ts... {
    using Ts::operator()...;
};

/// Deduction guide for `kira::overload`.
template <class... Ts> Overload(Ts...) -> Overload<Ts...>;

/// Helper struct for \c std::visit with lambdas.
///
/// Example: \code{.cpp}
/// static_assert(always_false_v<T>, "Unimplemented overload");
/// \endcode
template <class> inline constexpr bool always_false_v = false;
} // namespace kira
