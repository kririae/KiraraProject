#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Scene/Primitive.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"

namespace flux {
class Context;

/// \brief Host scene tables built from a Context.
///
/// Every array indexed by primitive uses the primitive indices assigned by
/// Context. An index without a visible primitive is a hole, and hiding a
/// primitive changes no index.
struct SceneTableData final : private Noncopyable {
    /// Entry of each primitive by primitive index. A hole is a default entry.
    std::vector<Primitive::Impl> primitives;

    /// Row-major object-to-world transforms by primitive index. A hole holds
    /// zeros, which no consumer reads.
    std::vector<std::array<float, 12>> transforms;

    /// BSDFs indexed by the indices assigned by Context. An unused index holds the
    /// empty BSDF.
    std::vector<BSDF::Impl> bsdfs;

    /// EDFs indexed by the indices assigned by Context. An unused index holds the
    /// empty EDF.
    std::vector<EDF::Impl> edfs;

public:
    /// \brief Rebuilds every table from \p context.
    void build(Context const &context);

    void clear() noexcept;
};
} // namespace flux
