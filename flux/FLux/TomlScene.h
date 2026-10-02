#pragma once

#include "flux/Scene/Context.h"
#include "flux/Scene/RenderProduct.h"

namespace flux {
struct FluxCLIRequest;

struct LoadedScene {
    Ref<Context> context;
    Ref<RenderProduct> product;
};

[[nodiscard]] LoadedScene loadTomlScene(FluxCLIRequest const &request);
} // namespace flux
