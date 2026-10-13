#include "flux/Scene/Light.h"

#include <numbers>

#include "flux/Core/Logging.h"
#include "flux/Core/MathUtils.h"

namespace flux {
namespace {
[[nodiscard]] Spectrum boundIntensity(Spectrum intensity) {
    for (auto &value : intensity)
        if (value < 0.0F) {
            LogWarn("PointLight: intensity component {} is negative, using 0", value);
            value = 0.0F;
        }
    return intensity;
}
} // namespace

Light::Light(TXContext &tx, LightType type) : RenderObject(tx), type_(type) {}

PointLight::PointLight(TXContext &tx, kira::Properties const &props) : Light(tx, LightType::Point) {
    position_ = props.use_or<Vec3f>("position", position_);
    intensity_ = boundIntensity(props.use_or<Spectrum>("intensity", intensity_));
}

void PointLight::setPosition(Vec3f const &position) {
    setIfDifferent(position_, position, DirtyBits::Position);
}

void PointLight::setIntensity(Spectrum const &intensity) {
    setIfDifferent(intensity_, boundIntensity(intensity), DirtyBits::Intensity);
}

PointLight::Impl PointLight::getImpl() const noexcept {
    return {
        .position = position_,
        .intensity = intensity_,
    };
}

float PointLight::estimatePower() const noexcept {
    return 4.0F * std::numbers::pi_v<float> * luminance(intensity_);
}
} // namespace flux
