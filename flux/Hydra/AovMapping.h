#pragma once

#include <pxr/imaging/hd/aov.h>

#include "flux/Scene/Film.h"

/// \brief Returns the film channel that \p binding asks for, or none if FLux has no such channel.
///
/// A source name of the source type \c raw, the default, takes the place of the AOV name.
[[nodiscard]] flux::FilmChannels HdFLuxGetAovChannel(pxr::HdRenderPassAovBinding const &binding);

/// \brief Returns the descriptor of the AOV \p name, or an empty descriptor if FLux has no such
/// AOV.
[[nodiscard]] pxr::HdAovDescriptor HdFLuxGetAovDescriptor(pxr::TfToken const &name);
