#include "flux/Scene/RenderProduct.h"

#include <utility>

#include "kira/Anyhow.h"

namespace flux {
Ref<RenderProduct> RenderProduct::create(Ref<Camera const> camera, kira::Properties const &props) {
    return Ref<RenderProduct>{new RenderProduct(std::move(camera), props)};
}

RenderProduct::RenderProduct(Ref<Camera const> camera, kira::Properties const &props)
    : film_([&] {
          auto const resolution = props.use<Vec2u>("resolution");
          return Film{resolution.x(), resolution.y()};
      }()),
      camera_(std::move(camera)), samplesPerPixel_(props.use_or<std::uint32_t>("num_samples", 1)) {
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
