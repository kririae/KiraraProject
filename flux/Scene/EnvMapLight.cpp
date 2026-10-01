#include "flux/Scene/EnvMapLight.h"

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
void clampScale(Spectrum &scale) noexcept {
    for (auto &component : scale)
        component = component > 0.0F ? component : 0.0F;
}
} // namespace

EnvMapLight::EnvMapLight(TXContext &tx, kira::Properties const &props)
    : Light(tx, LightType::EnvMap),
      texture_(Texture::resolve(tx, props, "texture", Spectrum{1.0F, 1.0F, 1.0F})) {
    if (props.is_type_of<float>("scale"))
        scale_ = Spectrum{props.use<float>("scale")};
    else
        scale_ = props.use_or<Spectrum>("scale", scale_);
    rotation_ = props.use_or<Vec3f>("rotation", rotation_);
    clampScale(scale_);
}

void EnvMapLight::setTexture(Ref<Texture const> texture) {
    if (!texture)
        throw kira::Anyhow("EnvMapLight: texture must not be null");
    if (!getContext() || texture->getContext() != getContext())
        throw kira::Anyhow("EnvMapLight: texture belongs to another context");
    setIfDifferent(texture_, texture, DirtyBits::Texture);
}

void EnvMapLight::setScale(Spectrum const &scale) {
    // Compare the clamped value, which is what the light stores.
    auto clamped = scale;
    clampScale(clamped);
    setIfDifferent(scale_, clamped, DirtyBits::Scale);
}

void EnvMapLight::setRotation(Vec3f const &rotation) {
    setIfDifferent(rotation_, rotation, DirtyBits::Rotation);
}

EnvMapLight::Impl EnvMapLight::getImpl(Distribution2D distribution) const {
    using Matrix = Eigen::Matrix<float, 3, 3, Eigen::RowMajor>;
    auto const radians = rotation_ * (std::numbers::pi_v<float> / 180.0F);
    auto const cx = std::cos(radians.x());
    auto const sx = std::sin(radians.x());
    auto const cy = std::cos(radians.y());
    auto const sy = std::sin(radians.y());
    auto const cz = std::cos(radians.z());
    auto const sz = std::sin(radians.z());
    Matrix const rotateX{{1.0F, 0.0F, 0.0F}, {0.0F, cx, sx}, {0.0F, -sx, cx}};
    Matrix const rotateY{{cy, 0.0F, -sy}, {0.0F, 1.0F, 0.0F}, {sy, 0.0F, cy}};
    Matrix const rotateZ{{cz, sz, 0.0F}, {-sz, cz, 0.0F}, {0.0F, 0.0F, 1.0F}};
    Matrix const worldToEnv{{0.0F, 0.0F, 1.0F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
    Matrix const transform = worldToEnv * rotateX * rotateY * rotateZ;

    auto result = Impl{
        .texture = texture_->getImpl(),
        .distribution = distribution,
        .scale = scale_,
        .worldToEnv = {},
    };
    std::copy_n(transform.data(), result.worldToEnv.size(), result.worldToEnv.begin());
    return result;
}

float EnvMapLight::estimatePower(float sceneRadius, float luminanceIntegral) const noexcept {
    return std::numbers::pi_v<float> * sceneRadius * sceneRadius * luminanceIntegral;
}
} // namespace flux
