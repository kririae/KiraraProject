#pragma once

#include "flux/Core/Math.h"
#include "flux/Scene/Camera.h"

namespace flux::test {
/// \brief Returns the screen window that makes a film of \p size show the pixels
/// [\p offset, \p offset + \p size) of a film of \p fullResolution with the default window.
///
/// The window is in the view of the film of \p size.
/// Its aspect ratio differs from the full film's.
[[nodiscard]] inline ScreenWindow cropWindow(Vec2u fullResolution, Vec2u offset, Vec2u size) {
    auto const fullWidth = static_cast<float>(fullResolution.x());
    auto const fullHeight = static_cast<float>(fullResolution.y());
    auto const aspectScale =
        (fullWidth / fullHeight) / (static_cast<float>(size.x()) / static_cast<float>(size.y()));
    auto const left = 2.0F * static_cast<float>(offset.x()) / fullWidth - 1.0F;
    auto const right = 2.0F * static_cast<float>(offset.x() + size.x()) / fullWidth - 1.0F;
    auto const top = 1.0F - 2.0F * static_cast<float>(offset.y()) / fullHeight;
    auto const bottom = 1.0F - 2.0F * static_cast<float>(offset.y() + size.y()) / fullHeight;
    return {
        .left = left * aspectScale,
        .right = right * aspectScale,
        .bottom = bottom,
        .top = top,
    };
}
} // namespace flux::test
