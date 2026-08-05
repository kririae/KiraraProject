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
/// The \c path property names an OBJ or PLY file. TriangleMesh owns its host arrays.
/// Each backend builds its geometry from these arrays.
///
/// PLY polygons are triangulated. Complete vertex normals and texture coordinates
/// are imported. Missing or incomplete normals are generated; missing or incomplete
/// texture coordinates are ignored.
class TriangleMesh final : public Geometry {
    friend class TXContext;

public:
    struct Impl;

    /// \brief Returns geometry-space vertex positions.
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

    /// \brief Returns geometry-space shading normals.
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

    /// \brief Computes the triangle-selection distribution for \p mesh.
    ///
    /// \pre Both output spans contain \c mesh.numTriangles elements.
    static void computeSamplingDistribution(
        Impl const &mesh, std::span<float> areaCDF, std::span<float> areaPDF
    );

    [[nodiscard]] float getSurfaceArea() const noexcept override { return surfaceArea_; }

private:
    TriangleMesh(TXContext &tx, kira::Properties const &props);

    void loadObj(std::filesystem::path const &path);
    void loadPly(std::filesystem::path const &path);

    /// Embree may read four floats for RTC_FORMAT_FLOAT3, so vertex storage
    /// includes one padding element.
    kira::SmallVector<Vec3f, 0> vertices_;
    kira::SmallVector<Vec3u, 0> triangles_;
    kira::SmallVector<Vec3f, 0> normals_;
    kira::SmallVector<Vec3u, 0> normalIndices_;
    kira::SmallVector<Vec2f, 0> texCoords_;
    kira::SmallVector<Vec3u, 0> texCoordIndices_;
    float surfaceArea_{};
};

/// \brief Stores indexed triangle mesh data for a backend scene.
///
/// The active backend keeps every referenced array alive while its scene uses
/// this Impl.
struct TriangleMesh::Impl {
    /// Array of geometry-space vertex positions.
    Vec3f const *vertices{};

    /// Array of zero-based triangle vertex indices.
    Vec3u const *triangles{};

    /// Array of geometry-space vertex normals, or null when absent.
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

    /// Cumulative geometry-space triangle areas, or null when unavailable.
    float const *triangleAreaCDF{};

    /// Geometry-space area densities, or null when unavailable.
    float const *triangleAreaPDF{};

    /// Total geometry-space surface area.
    float surfaceArea{};

public:
    [[nodiscard]] KIRA_HOST_DEVICE inline float
    getTriangleArea(std::uint32_t triangle) const noexcept;

    /// \brief Interpolates the shading normal, or returns \p geometricNormal.
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec3f interpolateShadingNormal(
        PreliminaryIntersection const &preliminary, Vec3f const &geometricNormal
    ) const noexcept;

    /// \brief Interpolates texture coordinates, or returns zero when absent.
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec2f
    interpolateTexCoord(PreliminaryIntersection const &preliminary) const noexcept;

    /// \brief Reconstructs a geometry-space interaction from \p preliminary.
    ///
    /// \pre \p preliminary names a valid, non-degenerate triangle.
    [[nodiscard]] KIRA_HOST_DEVICE inline GeometryInteraction
    computeInteraction(PreliminaryIntersection const &preliminary) const noexcept;

    /// \brief Samples the surface with respect to geometry-space area.
    [[nodiscard]] KIRA_HOST_DEVICE inline GeometrySample sample(Vec2f const &sample) const noexcept;

    /// \brief Returns the geometry-space area density used by \c sample.
    [[nodiscard]] KIRA_HOST_DEVICE inline float pdf(std::uint32_t triangle) const noexcept;
};

static_assert(std::is_standard_layout_v<TriangleMesh::Impl>);
static_assert(std::is_trivially_copyable_v<TriangleMesh::Impl>);

namespace optix {
/// OptiX alias for TriangleMesh::Impl.
using TriangleMesh = ::flux::TriangleMesh::Impl;
} // namespace optix
} // namespace flux
