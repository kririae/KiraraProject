#pragma once

#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
#define HDFLUX_RENDER_SETTINGS_TOKENS                                                              \
    ((denoise, "flux:denoise"))                                                                    \
    ((denoiseStartSample, "flux:denoiseStartSample"))                                              \
    ((interactiveSamplesPerLaunch, "flux:interactiveSamplesPerLaunch"))                            \
    ((launchesPerDenoise, "flux:launchesPerDenoise"))                                              \
    ((maxDepth, "flux:maxDepth"))                                                                  \
    ((rrDepth, "flux:rrDepth"))                                                                    \
    ((rrProb, "flux:rrProb"))                                                                      \
    ((sampler, "flux:sampler"))                                                                    \
    ((shaderReorder, "flux:shaderReorder"))
// clang-format on

TF_DECLARE_PUBLIC_TOKENS(HdFLuxRenderSettingsTokens, HDFLUX_RENDER_SETTINGS_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE

/// The defaults of the render settings.
inline constexpr int HdFLuxDefaultConvergedSamplesPerPixel{128};
inline constexpr int HdFLuxDefaultInteractiveSamplesPerLaunch{4};
inline constexpr bool HdFLuxDefaultDenoise{false};
inline constexpr int HdFLuxDefaultDenoiseStartSample{2};
inline constexpr int HdFLuxDefaultLaunchesPerDenoise{4};
