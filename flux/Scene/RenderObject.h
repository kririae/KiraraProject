#pragma once

#include "flux/Core/Object.h"

namespace flux {
/// \brief A configurable object linked after its creation transaction commits.
class RenderObject : public ConfigurableObject {
protected:
    /// \brief Constructs a render object in \p tx.
    RenderObject(TXContext &tx, kira::Properties properties);

    void registerTo(TXContext &tx) override;

public:
    /// \brief Resolves references that require the object to be in its context.
    virtual void link() {}
};
} // namespace flux
