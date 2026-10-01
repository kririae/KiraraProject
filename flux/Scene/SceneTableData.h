#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/SmallVector.h"

namespace flux {
class Context;

/// \brief Owns the host scene tables built from a Context.
///
/// \c primitives and \c transforms share one dense primitive order: the visible
/// primitives of the Context ordered by Context ID. That order is the dense
/// primitive index, so it selects entries of every array indexed by primitive.
///
/// The dense geometry and primitive indices are assigned here.
struct SceneTableData final : private Noncopyable {
    /// Visible primitives in dense primitive order.
    std::vector<Primitive::Impl> primitives;

    /// Row-major object-to-world transforms in dense primitive order.
    std::vector<std::array<float, 12>> transforms;

    /// Unique meshes in dense geometry order.
    kira::SmallVector<Ref<TriangleMesh const>> meshes;

    /// BSDFs indexed by the indices assigned by Context.
    std::vector<BSDF::Impl> bsdfs;

    /// EDFs indexed by the indices assigned by Context.
    std::vector<EDF::Impl> edfs;

public:
    /// \brief Rebuilds every table from \p context.
    ///
    /// \throw kira::Anyhow If a geometry is not a supported type, or if a table
    ///        would exceed the dense index range.
    void build(Context const &context);

    void clear() noexcept;
};
} // namespace flux
