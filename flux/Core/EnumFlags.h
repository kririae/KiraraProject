#pragma once

#include <bit>
#include <type_traits>

namespace flux {
/// \brief Marks scoped enumeration \c E as a set of bit flags.
///
/// Specialize it to true to enable \c operator| , \c operator& , and \c any for \c E.
template <typename E> inline constexpr bool isEnumFlags = false;

template <typename E>
    requires(isEnumFlags<E>)
[[nodiscard]] constexpr E operator|(E lhs, E rhs) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(lhs) | static_cast<U>(rhs));
}

template <typename E>
    requires(isEnumFlags<E>)
[[nodiscard]] constexpr E operator&(E lhs, E rhs) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(lhs) & static_cast<U>(rhs));
}

/// \brief Returns whether any bit of \p flags is set.
template <typename E>
    requires(isEnumFlags<E>)
[[nodiscard]] constexpr bool any(E flags) noexcept {
    return static_cast<std::underlying_type_t<E>>(flags) != 0;
}

/// \brief Calls \p function once for each bit set in \p flags, from the lowest bit up.
///
/// Each call receives a value of \c E with one bit set, so the callee can \c switch over the
/// enumerators of \c E.
template <typename E, typename F>
    requires(isEnumFlags<E>)
constexpr void forEachBit(E flags, F &&function) {
    using U = std::make_unsigned_t<std::underlying_type_t<E>>;
    auto bits = static_cast<U>(flags);
    while (bits != 0) {
        auto const lowest = static_cast<U>(U{1} << std::countr_zero(bits));
        function(static_cast<E>(lowest));
        bits = static_cast<U>(bits ^ lowest);
    }
}
} // namespace flux
