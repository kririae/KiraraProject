#include "flux/Hydra/DomeLight.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/sdf/assetPath.h>

#include <Eigen/LU>
#include <Eigen/SVD>
#include <array>
#include <exception>
#include <filesystem>
#include <utility>

#include "flux/Hydra/LightEmission.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/Transform.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Shading/Texture.h"
#include "kira/Properties.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
/// Returns the rotation of the env map of a dome with \p transform.
///
/// It is the rotation nearest to the linear part of \p transform, then a turn of pi about +Y.
/// UsdLux's latlong and the env map's differ by that turn.
[[nodiscard]] std::array<float, 9> computeEnvMapRotation(std::array<float, 12> const &transform) {
    Eigen::Matrix3d linear;
    for (auto row = 0; row < 3; ++row)
        for (auto column = 0; column < 3; ++column)
            linear(row, column) = transform[4 * row + column];

    Eigen::JacobiSVD<Eigen::Matrix3d> const svd{linear, Eigen::ComputeFullU | Eigen::ComputeFullV};
    Eigen::Matrix3d left = svd.matrixU();
    Eigen::Matrix3d const right = svd.matrixV();
    if ((left * right.transpose()).determinant() < 0.0)
        left.col(2) *= -1.0;

    Eigen::Matrix3d const turnAboutY = Eigen::Vector3d{-1.0, 1.0, -1.0}.asDiagonal();
    Eigen::Matrix3d const rotation = left * right.transpose() * turnAboutY;

    std::array<float, 9> result{};
    for (auto row = 0; row < 3; ++row)
        for (auto column = 0; column < 3; ++column)
            result[3 * row + column] = static_cast<float>(rotation(row, column));
    return result;
}
} // namespace

HdFluxDomeLight::HdFluxDomeLight(SdfPath const &id) : HdLight(id) {}

HdFluxDomeLight::~HdFluxDomeLight() = default;

void HdFluxDomeLight::Sync(
    HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits
) {
    auto const &id = GetId();
    auto &param = *static_cast<HdFluxRenderParam *>(renderParam);

    try {
        auto const context = param.GetContext();
        if (!envMap_) {
            envMap_ = context->create<flux::EnvMapLight>(kira::Properties{});
            defaultTexture_ = envMap_->getTexture();
        }

        syncTexture(*context, *sceneDelegate);
        envMap_->setScale(HdFluxComputeLightEmission(*sceneDelegate, id));
        envMap_->setRotation(
            computeEnvMapRotation(HdFluxToTransform(sceneDelegate->GetTransform(id)))
        );
        syncActiveEnvMap(*context, sceneDelegate->GetVisible(id));
    } catch (std::exception const &error) { TF_WARN("%s: %s", id.GetText(), error.what()); }

    *dirtyBits &= ~AllDirty;
}

HdDirtyBits HdFluxDomeLight::GetInitialDirtyBitsMask() const { return AllDirty; }

void HdFluxDomeLight::Finalize(HdRenderParam *renderParam) {
    if (!envMap_)
        return;

    auto const context = static_cast<HdFluxRenderParam *>(renderParam)->GetContext();
    context->remove(envMap_->getContextId());
}

void HdFluxDomeLight::syncTexture(flux::Context &context, HdSceneDelegate &sceneDelegate) {
    auto const asset = sceneDelegate.GetLightParamValue(GetId(), HdLightTokens->textureFile)
                           .GetWithDefault(SdfAssetPath{});
    auto path = asset.GetResolvedPath();
    if (path.empty())
        path = asset.GetAssetPath();
    if (path == texturePath_)
        return;

    if (path.empty()) {
        envMap_->setTexture(defaultTexture_);
    } else {
        kira::Properties props;
        props.set("type", "image");
        props.set("path", std::filesystem::path{path});
        envMap_->setTexture(context.create<flux::Texture>(props));
    }
    texturePath_ = std::move(path);
}

void HdFluxDomeLight::syncActiveEnvMap(flux::Context &context, bool visible) {
    auto const active = context.getActiveEnvMap();
    if (!visible) {
        if (active == envMap_)
            context.setActiveEnvMap({});
        return;
    }

    if (active && active != envMap_)
        TF_WARN("%s: replaces the dome light that renders", GetId().GetText());
    context.setActiveEnvMap(envMap_);
}
