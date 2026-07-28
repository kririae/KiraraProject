#pragma once

#include <cstdint>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"

namespace flux {
/// \brief Identifies a concrete geometry implementation.
enum class GeometryType : std::uint8_t {
    TriangleMesh,
    Count,
};

/// \brief Traversal result needed to reconstruct a surface intersection.
///
/// Coordinates are interpreted by the concrete geometry. For a triangle mesh,
/// they are the weights of the second and third vertices reported by OptiX.
struct PreliminaryIntersection {
    /// Ray parameter at the reported hit.
    float distance{};

    /// Geometry-defined coordinates reported by traversal.
    Vec2f coordinates{};

    /// Element index within the concrete geometry.
    std::uint32_t elementIndex{};
};

/// \brief Geometry-space data reconstructed at a surface hit.
struct GeometryInteraction {
    /// Geometry-space hit position.
    Vec3f position{};

    /// Geometry-space normal derived from the underlying surface.
    Vec3f geometricNormal{};

    /// Geometry-space normal used to initialize shading.
    Vec3f shadingNormal{};

    /// Surface parameterization, or zero when the geometry has no texture coordinates.
    Vec2f uv{};

    /// Element index within the concrete geometry.
    std::uint32_t elementIndex{};
};

/// \brief Host-side base for geometry stored in a scene.
class Geometry : public RenderObject {
protected:
    /// \brief Creates geometry of \p type in \p tx.
    Geometry(TXContext &tx, kira::Properties properties, GeometryType type);

public:
    /// \brief Returns the concrete geometry type.
    [[nodiscard]] GeometryType getType() const noexcept { return type_; }

private:
    GeometryType type_;
};

static_assert(std::is_standard_layout_v<PreliminaryIntersection>);
static_assert(std::is_trivially_copyable_v<PreliminaryIntersection>);
static_assert(std::is_standard_layout_v<GeometryInteraction>);
static_assert(std::is_trivially_copyable_v<GeometryInteraction>);
} // namespace flux
