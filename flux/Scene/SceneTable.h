#pragma once

#include <cstdint>
#include <type_traits>

#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/Primitive.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Dense tables of the geometries, primitives, BSDFs, and EDFs of one
///        scene.
///
/// An index selects an entry of a table here. Pointers address host or device
/// memory.
struct SceneTable {
    /// Unique geometries indexed by dense geometry index.
    Geometry::Impl const *geometries{};

    /// Primitives indexed by the indices assigned by Context. An index without a
    /// visible primitive holds a hole.
    Primitive::Impl const *primitives{};

    /// BSDFs indexed by the indices assigned by Context.
    BSDF::Impl const *bsdfs{};

    /// EDFs indexed by the indices assigned by Context.
    EDF::Impl const *edfs{};

public:
    /// \brief Returns the primitive at \p index.
    ///
    /// \pre \p index addresses a visible primitive of the scene.
    [[nodiscard]] KIRA_HOST_DEVICE Primitive::Impl const &
    getPrimitive(std::uint32_t index) const noexcept {
        return primitives[index];
    }

    /// \brief Returns the geometry at dense \p index.
    ///
    /// \pre \p index addresses a geometry of the scene.
    [[nodiscard]] KIRA_HOST_DEVICE Geometry::Impl const &
    getGeometry(std::uint32_t index) const noexcept {
        return geometries[index];
    }

    /// \brief Returns the BSDF at \p index.
    ///
    /// \pre \p index addresses a BSDF of the scene.
    [[nodiscard]] KIRA_HOST_DEVICE BSDF::Impl const &getBSDF(std::uint32_t index) const noexcept {
        return bsdfs[index];
    }

    /// \brief Returns the EDF at \p index.
    ///
    /// \pre \p index addresses an EDF of the scene.
    [[nodiscard]] KIRA_HOST_DEVICE EDF::Impl const &getEDF(std::uint32_t index) const noexcept {
        return edfs[index];
    }
};

static_assert(std::is_standard_layout_v<SceneTable>);
static_assert(std::is_trivially_copyable_v<SceneTable>);
} // namespace flux
