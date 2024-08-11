#pragma once

#include "kira/Vecteur.h"

namespace kira {
template <typename Scalar, size_t Size, typename F> auto InstantiateStaticTests(F &&f) {
    f.template operator()<Vecteur<Scalar, Size, VecteurBackend::Generic>>();
    f.template operator()<Vecteur<Scalar, Size, VecteurBackend::Lazy>>();
}

template <typename Scalar, typename F> auto InstantiateDynamicTests(F &&f) {
    f.template operator()<Vecteur<Scalar, std::dynamic_extent, VecteurBackend::Generic>>();
    f.template operator()<Vecteur<Scalar, std::dynamic_extent, VecteurBackend::Lazy>>();
}
} // namespace kira
