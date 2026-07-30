#include "flux/Scene/Light.h"

#include "kira/Anyhow.h"

namespace flux {
namespace {
void validatePosition(Vec3f const &position) {
    for (auto const value : position)
        if (!std::isfinite(value))
            throw kira::Anyhow("PointLight: position must be finite");
}

void validateIntensity(Spectrum const &intensity) {
    for (auto const value : intensity)
        if (!(value >= 0.0F && std::isfinite(value)))
            throw kira::Anyhow("PointLight: intensity must be finite and nonnegative");
}
} // namespace

Light::Light(TXContext &tx, LightType type) : RenderObject(tx), type_(type) {}

PointLight::PointLight(TXContext &tx, kira::Properties const &props) : Light(tx, LightType::Point) {
    position_ = props.use_or<Vec3f>("position", position_);
    intensity_ = props.use_or<Spectrum>("intensity", intensity_);
    validatePosition(position_);
    validateIntensity(intensity_);
}

void PointLight::setPosition(Vec3f const &position) {
    validatePosition(position);
    position_ = position;
}

void PointLight::setIntensity(Spectrum const &intensity) {
    validateIntensity(intensity);
    intensity_ = intensity;
}

PointLight::Impl PointLight::getImpl() const noexcept {
    return {
        .position = position_,
        .intensity = intensity_,
    };
}
} // namespace flux
