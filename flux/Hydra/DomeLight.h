#pragma once

#include <pxr/imaging/hd/light.h>

#include <string>

#include "flux/Core/Object.h"

namespace flux {
class Context;
class EnvMapLight;
class Texture;
} // namespace flux

/// \brief A Hydra dome light that renders as the environment map of the Context.
class HdFluxDomeLight final : public pxr::HdLight {
public:
    explicit HdFluxDomeLight(pxr::SdfPath const &id);
    ~HdFluxDomeLight() override;

    void Sync(
        pxr::HdSceneDelegate *sceneDelegate, pxr::HdRenderParam *renderParam,
        pxr::HdDirtyBits *dirtyBits
    ) override;

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Finalize(pxr::HdRenderParam *renderParam) override;

private:
    void syncTexture(flux::Context &context, pxr::HdSceneDelegate &sceneDelegate);
    void syncActive(flux::Context &context, bool visible);

    /// Null until a sync creates it.
    flux::Ref<flux::EnvMapLight> envMap_;

    flux::Ref<flux::Texture const> defaultTexture_;

    /// The file of the texture of \c envMap_, empty for \c defaultTexture_.
    std::string texturePath_;
};
