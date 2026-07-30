#pragma once

#include "flux/Core/Object.h"

namespace flux {
/// \brief A configurable object used to describe a scene.
class RenderObject : public ConfigurableObject {
protected:
    /// \brief Constructs a render object in \p tx.
    explicit RenderObject(TXContext &tx);
};
} // namespace flux
