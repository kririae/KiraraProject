#pragma once

#include <cstdint>

#include "flux/Core/Math.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Film.h"

namespace flux {
struct FluxCLIOptions;

/// \brief A scene and the image to render from it.
struct LoadedScene {
    Ref<Context> context;
    Ref<Camera const> camera;
    FilmSpec film;

    /// The target number of samples per pixel.
    std::uint32_t samplesPerPixel;
};

/// \brief Loads the TOML scene that \p options names.
///
/// \par Film properties
/// - \c resolution: required uint32 image resolution.
/// - \c num_samples: optional uint32 target sample count, defaults to 1.
[[nodiscard]] LoadedScene loadTomlScene(FluxCLIOptions const &options);
} // namespace flux
