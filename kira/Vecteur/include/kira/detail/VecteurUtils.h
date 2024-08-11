#pragma once

#include "kira/VecteurTraits.h"

namespace kira::detail {
template <typename T> static constexpr auto entry_or_scalar(T const &obj, auto i) {
    if constexpr (is_vecteur<T>)
        return obj.entry(i);
    else
        return obj;
}

template <typename T> static constexpr auto size_or_1(T const &obj) {
    if constexpr (is_vecteur<T>)
        return obj.size();
    else
        return 1UL;
}
} // namespace kira::detail
