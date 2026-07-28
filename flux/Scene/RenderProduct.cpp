#include "flux/Scene/RenderProduct.h"

#include <utility>

#include "kira/Anyhow.h"

namespace flux {
Ref<RenderProduct> RenderProduct::create(Ref<Camera const> camera, kira::Properties properties) {
    return Ref<RenderProduct>{new RenderProduct(std::move(camera), std::move(properties))};
}

RenderProduct::RenderProduct(Ref<Camera const> camera, kira::Properties properties)
    : film_(properties.use<std::uint32_t>("width"), properties.use<std::uint32_t>("height")),
      camera_(std::move(camera)),
      samplesPerPixel_(properties.use_or<std::uint32_t>("num_samples", 1)) {
    if (!camera_)
        throw kira::Anyhow("RenderProduct: camera must not be null");
    if (samplesPerPixel_ == 0)
        throw kira::Anyhow("RenderProduct: sample count must be nonzero");
}

void RenderProduct::setCamera(Ref<Camera const> camera) {
    if (!camera)
        throw kira::Anyhow("RenderProduct: camera must not be null");
    camera_ = std::move(camera);
}

void RenderProduct::setSamplesPerPixel(std::uint32_t samples) {
    if (samples == 0)
        throw kira::Anyhow("RenderProduct: sample count must be nonzero");
    samplesPerPixel_ = samples;
}
} // namespace flux
