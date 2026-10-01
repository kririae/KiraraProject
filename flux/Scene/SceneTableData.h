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

/// \brief Host scene tables built from a Context.
///
/// The dense primitive order is the visible primitives in Context ID order. Every
/// array indexed by primitive uses it.
struct SceneTableData final : private Noncopyable {
    /// Context object at each dense index. Host only; not read while rendering.
    struct {
        /// Visible primitives in dense primitive order.
        kira::SmallVector<Ref<Primitive const>> primitives;

        /// Unique meshes in dense geometry order.
        kira::SmallVector<Ref<TriangleMesh const>> meshes;
    } objects;

    /// Entry of each visible primitive in dense primitive order.
    std::vector<Primitive::Impl> primitives;

    /// Row-major object-to-world transforms in dense primitive order.
    std::vector<std::array<float, 12>> transforms;

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
