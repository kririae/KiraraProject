#pragma once

#include "kira/Vecteur/Base.h"
#include "kira/Vecteur/Format.h"
#include "kira/Vecteur/Generic.h"
#include "kira/Vecteur/Highway.h"
#include "kira/Vecteur/Lazy.h"
#include "kira/Vecteur/Router.h"
#include "kira/Vecteur/Traits.h"

namespace kira {
using vecteur::is_leaf_vecteur;
using vecteur::is_vecteur;
using vecteur::Vecteur;
using vecteur::VecteurBackend;

// Unless otherwise specified, use the generic backend for now.
// "Application speedups through core library changes"
constexpr auto defaultBackend = VecteurBackend::Generic;

using VecXi = Vecteur<int, std::dynamic_extent, defaultBackend>;
using VecXf = Vecteur<float, std::dynamic_extent, defaultBackend>;
using VecXd = Vecteur<double, std::dynamic_extent, defaultBackend>;

using Vec1i = Vecteur<int, 1, defaultBackend>;
using Vec2i = Vecteur<int, 2, defaultBackend>;
using Vec3i = Vecteur<int, 3, defaultBackend>;
using Vec4i = Vecteur<int, 4, defaultBackend>;

using Vec1f = Vecteur<float, 1, defaultBackend>;
using Vec2f = Vecteur<float, 2, defaultBackend>;
using Vec3f = Vecteur<float, 3, defaultBackend>;
using Vec4f = Vecteur<float, 4, defaultBackend>;

using Vec1d = Vecteur<double, 1, defaultBackend>;
using Vec2d = Vecteur<double, 2, defaultBackend>;
using Vec3d = Vecteur<double, 3, defaultBackend>;
using Vec4d = Vecteur<double, 4, defaultBackend>;
} // namespace kira
