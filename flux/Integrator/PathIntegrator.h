#pragma once

#include "flux/Scene/RenderObject.h"

namespace flux {
/// \brief Selects path tracing as a context's transport algorithm.
///
/// The first path integrator added to a context becomes active after its
/// transaction succeeds.
class PathIntegrator final : public RenderObject {
    friend class TXContext;

public:
    /// \brief Device-side path-integrator operations.
    struct DeviceImpl;

private:
    /// \brief Constructs a path integrator in \p tx.
    PathIntegrator(TXContext &tx, kira::Properties properties);

    /// \copydoc ContextObject::registerTo
    void registerTo(TXContext &tx) override;
};
} // namespace flux
