#pragma once

#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
#define HDFLUX_RENDER_SETTINGS_TOKENS                                                              \
    ((interactiveSamplesPerLaunch, "flux:interactiveSamplesPerLaunch"))                            \
    ((maxDepth, "flux:maxDepth"))                                                                  \
    ((rrDepth, "flux:rrDepth"))                                                                    \
    ((rrProb, "flux:rrProb"))                                                                      \
    ((sampler, "flux:sampler"))                                                                    \
    ((shaderReorder, "flux:shaderReorder"))
// clang-format on

TF_DECLARE_PUBLIC_TOKENS(HdFluxRenderSettingsTokens, HDFLUX_RENDER_SETTINGS_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE

/// The defaults of the render settings.
inline constexpr int HdFluxDefaultConvergedSamplesPerPixel{128};
inline constexpr int HdFluxDefaultInteractiveSamplesPerLaunch{4};
