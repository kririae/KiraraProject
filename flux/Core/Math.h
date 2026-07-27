#pragma once

#include <cstdint>

#include "kira/Vecteur.h"

namespace flux {
/// Three-component floating-point vector.
using Vec3f = kira::Vec3f;

/// Three-component unsigned integer vector.
using Vec3u = kira::Vecteur<std::uint32_t, 3, kira::defaultBackend>;
} // namespace flux
