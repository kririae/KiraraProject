#pragma once

#include <filesystem>
#include <span>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Scene/Geometry.h"
#include "kira/Compiler.h"
#include "kira/SmallVector.h"

namespace flux {
/// \brief Host indexed triangle mesh.
///
/// The \c path property names an OBJ file. TriangleMesh owns its host arrays.
/// Each backend builds its geometry from these arrays.
class TriangleMesh final : public Geometry {
    friend class TXContext;

public:
    struct Impl;

    /// \brief Returns object-space vertex positions.
    [[nodiscard]] std::span<Vec3f const> getVertices() const noexcept {
        return {
            vertices_.data(),
            vertices_.empty() ? 0 : vertices_.size() - 1,
        };
    }

    /// \brief Returns zero-based vertex indices for each triangle.
    [[nodiscard]] std::span<Vec3u const> getTriangles() const noexcept {
        return {triangles_.data(), triangles_.size()};
    }

    /// \brief Returns object-space shading normals.
    [[nodiscard]] std::span<Vec3f const> getNormals() const noexcept {
        return {normals_.data(), normals_.size()};
    }

    /// \brief Returns face-varying normal indices.
    ///
    /// An empty span with nonempty normals means the vertex indices also index
    /// the normals.
    [[nodiscard]] std::span<Vec3u const> getNormalIndices() const noexcept {
        return {normalIndices_.data(), normalIndices_.size()};
    }

    /// \brief Returns texture coordinates, or an empty span when absent.
    [[nodiscard]] std::span<Vec2f const> getTexCoords() const noexcept {
        return {texCoords_.data(), texCoords_.size()};
    }

    /// \brief Returns face-varying texture-coordinate indices.
    ///
    /// An empty span with nonempty texture coordinates means the vertex indices
    /// also index the texture coordinates.
    [[nodiscard]] std::span<Vec3u const> getTexCoordIndices() const noexcept {
        return {texCoordIndices_.data(), texCoordIndices_.size()};
    }

    /// \brief Returns an Impl that refers to the host mesh arrays.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    TriangleMesh(TXContext &tx, kira::Properties properties);

    /// \brief Replaces this mesh with the triangulated contents of \p path.
    void loadObj(std::filesystem::path const &path);

    /// Embree may read four floats for RTC_FORMAT_FLOAT3, so vertex storage
    /// includes one padding element.
    kira::SmallVector<Vec3f, 0> vertices_;
    kira::SmallVector<Vec3u, 0> triangles_;
    kira::SmallVector<Vec3f, 0> normals_;
    kira::SmallVector<Vec3u, 0> normalIndices_;
    kira::SmallVector<Vec2f, 0> texCoords_;
    kira::SmallVector<Vec3u, 0> texCoordIndices_;
};

/// \brief Stores indexed triangle mesh data for a backend scene.
///
/// The active backend keeps every referenced array alive while its scene uses
/// this Impl.
struct TriangleMesh::Impl {
    /// Array of object-space vertex positions.
    Vec3f const *vertices{};

    /// Array of zero-based triangle vertex indices.
    Vec3u const *triangles{};

    /// Array of object-space vertex normals, or null when absent.
    Vec3f const *normals{};

    /// Per-triangle normal indices, valid whenever \c normals is non-null.
    Vec3u const *normalIndices{};

    /// Array of texture coordinates, or null when absent.
    Vec2f const *texCoords{};

    /// Per-triangle texture-coordinate indices, valid whenever \c texCoords is non-null.
    Vec3u const *texCoordIndices{};

    /// Number of elements in \c vertices.
    std::uint32_t numVertices{};

    /// Number of elements in \c triangles.
    std::uint32_t numTriangles{};

public:
    /// \brief Returns one vertex of \p triangle.
    ///
    /// \pre \p triangle is less than \c numTriangles and \p corner is less
    /// than three.
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec3f
    getVertex(std::uint32_t triangle, std::uint32_t corner) const noexcept;

    /// \brief Reconstructs a geometry-space interaction from \p preliminary.
    ///
    /// \pre \p preliminary names a valid, non-degenerate triangle.
    [[nodiscard]] KIRA_HOST_DEVICE inline GeometryInteraction
    computeInteraction(PreliminaryIntersection const &preliminary) const noexcept;
};

static_assert(std::is_standard_layout_v<TriangleMesh::Impl>);
static_assert(std::is_trivially_copyable_v<TriangleMesh::Impl>);

namespace optix {
/// OptiX alias for TriangleMesh::Impl.
using TriangleMesh = ::flux::TriangleMesh::Impl;
} // namespace optix
} // namespace flux
