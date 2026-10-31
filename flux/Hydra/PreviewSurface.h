#pragma once

#include <pxr/base/gf/vec3f.h>

namespace flux {
class Context;
class PrincipledBSDF;
} // namespace flux

/// \brief The inputs of a UsdPreviewSurface that hdFlux renders, at the defaults of its spec.
struct HdFluxPreviewSurface {
    pxr::GfVec3f diffuseColor{0.18F};
    float roughness{0.5F};
    float metallic{0.0F};
    float ior{1.5F};
};

/// \brief Writes every parameter of \p bsdf: those that \p surface gives, and zero for the rest.
///
/// If it throws, \p bsdf is unchanged.
///
/// \pre \p bsdf belongs to \p context.
void HdFluxWritePreviewSurface(
    flux::Context &context, flux::PrincipledBSDF &bsdf, HdFluxPreviewSurface const &surface
);
