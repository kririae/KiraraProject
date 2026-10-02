#pragma once

#include <filesystem>
#include <span>
#include <type_traits>

#include "flux/Core/HostBuffer.h"
#include "flux/Core/Math.h"
#include "flux/Scene/Geometry.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Host indexed triangle mesh.
///
/// A mesh is built either from shared host arrays or from the OBJ or PLY file
/// named by the \c path property. TriangleMesh shares its host arrays with
/// whoever built it, and each backend builds its geometry from them.
///
/// PLY polygons are triangulated. Complete vertex normals and texture coordinates
/// are imported. Missing or incomplete normals are generated; missing or incomplete
/// texture coordinates are ignored.
class TriangleMesh final : public Geometry {
    friend class TXContext;

public:
    struct Impl;

    /// \brief Shared host arrays a triangle mesh is built from.
    ///
    /// \c triangles indexes \c vertices. Present \c normals are indexed by
    /// \c normalIndices, or by \c triangles when those are absent; \c texCoords
    /// and \c texCoordIndices pair with \c triangles the same way. Absent
    /// \c normals request generated angle-weighted vertex normals, and an index
    /// array whose attribute array is absent is not a mesh.
    ///
    /// \c vertices and \c triangles are required. Each other array is optional,
    /// and absent when its handle is null or its buffer is empty.
    struct Data {
        /// Geometry-space vertex positions, nonempty. The buffer must have spare
        /// capacity, \c capacity() > \c size(): Embree reads past the last vertex.
        Shared<HostBuffer<Vec3f>> vertices;

        /// Zero-based vertex indices of each triangle, nonempty.
        Shared<HostBuffer<Vec3u>> triangles;

        /// Geometry-space shading normals.
        Shared<HostBuffer<Vec3f>> normals;

        /// Normal indices of each triangle.
        Shared<HostBuffer<Vec3u>> normalIndices;

        /// Texture coordinates.
        Shared<HostBuffer<Vec2f>> texCoords;

        /// Texture-coordinate indices of each triangle.
        Shared<HostBuffer<Vec3u>> texCoordIndices;
    };

    /// \brief Checks that every index in \p data addresses its array.
    ///
    /// Construction does not check indices, so a caller that did not produce
    /// \p data itself calls this first. Reporting the source of \p data is left
    /// to that caller, which knows it.
    ///
    /// \throw kira::Anyhow If \p data has no vertices or no triangles, an index
    ///        is out of range, or an index array is nonempty and does not have
    ///        one entry per triangle.
    static void checkIndices(Data const &data);

    /// \brief Returns the shared arrays the mesh was built from.
    ///
    /// \c vertices, \c triangles, and \c normals are never null; generated
    /// normals replace absent ones. Another handle is null exactly when its
    /// array is absent.
    [[nodiscard]] Data const &getData() const noexcept { return data_; }

    /// \brief Returns geometry-space vertex positions.
    [[nodiscard]] std::span<Vec3f const> getVertices() const noexcept {
        return view(data_.vertices);
    }

    /// \brief Returns zero-based vertex indices for each triangle.
    [[nodiscard]] std::span<Vec3u const> getTriangles() const noexcept {
        return view(data_.triangles);
    }

    /// \brief Returns geometry-space shading normals.
    [[nodiscard]] std::span<Vec3f const> getNormals() const noexcept { return view(data_.normals); }

    /// \brief Returns face-varying normal indices.
    ///
    /// An empty span with nonempty normals means the vertex indices also index
    /// the normals.
    [[nodiscard]] std::span<Vec3u const> getNormalIndices() const noexcept {
        return view(data_.normalIndices);
    }

    /// \brief Returns texture coordinates, or an empty span when absent.
    [[nodiscard]] std::span<Vec2f const> getTexCoords() const noexcept {
        return view(data_.texCoords);
    }

    /// \brief Returns face-varying texture-coordinate indices.
    ///
    /// An empty span with nonempty texture coordinates means the vertex indices
    /// also index the texture coordinates.
    [[nodiscard]] std::span<Vec3u const> getTexCoordIndices() const noexcept {
        return view(data_.texCoordIndices);
    }

    /// \brief Returns an Impl that refers to the host mesh arrays.
    [[nodiscard]] Impl getImpl() const noexcept;

    /// \brief Returns an Impl that refers to the arrays of \p data.
    ///
    /// \p surfaceArea is the total geometry-space area of the mesh. The Impl
    /// holds no handle, so \p data must outlive it.
    [[nodiscard]] static Impl makeImpl(Data const &data, float surfaceArea) noexcept;

    /// \brief Computes the triangle-selection distribution for \p mesh.
    ///
    /// \pre Both output spans contain \c mesh.numTriangles elements.
    static void computeSamplingDistribution(
        Impl const &mesh, std::span<float> areaCDF, std::span<float> areaPDF
    );

    [[nodiscard]] float getSurfaceArea() const noexcept override { return surfaceArea_; }

private:
    /// \brief Builds a mesh from shared host arrays.
    ///
    /// \pre Every index in \p data addresses its array, which
    ///      \c checkIndices establishes.
    /// \throw kira::Anyhow If \p data has no vertices or no triangles, or holds
    ///        more vertices or triangles than a mesh can address.
    TriangleMesh(TXContext &tx, Data &&data);

    /// \brief Builds a mesh from the file named by the \c path property.
    TriangleMesh(TXContext &tx, kira::Properties const &props);

    /// \brief Returns the elements of \p buffer, or an empty span when it is null.
    template <typename T>
    [[nodiscard]] static std::span<T const> view(Shared<HostBuffer<T>> const &buffer) noexcept {
        return buffer ? buffer->span() : std::span<T const>{};
    }

    /// Arrays built from, with generated normals in place of absent ones.
    Data data_;
    float surfaceArea_{};
};

/// \brief Indexed triangle mesh used during rendering.
///
/// The backend keeps the referenced arrays valid until it rebuilds the scene.
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
