#pragma once

#include <pxr/base/gf/matrix4d.h>

#include <optional>

#include "flux/Core/Math.h"
#include "flux/Scene/Film.h"

/// \brief What a renderer renders: the resolution and the channels of the image, and the view to
/// see from.
///
/// Nothing is rendered if \c channels is empty, or if \c view is empty.
struct HdFluxRenderSpec {
    /// \brief The camera that sees the scene.
    struct View {
        pxr::GfMatrix4d worldToView;
        pxr::GfMatrix4d projection;

        bool operator==(View const &) const = default;
    };

    flux::Vec2u resolution{0U, 0U};
    flux::FilmChannels channels{flux::FilmChannels::None};
    std::optional<View> view;

    bool operator==(HdFluxRenderSpec const &) const = default;
};
