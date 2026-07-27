#pragma once

#include <filesystem>
#include <span>
#include <type_traits>
#include <vector>

#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Host-side indexed triangle mesh.
///
/// The \c path property names an OBJ file. This class owns CPU data only;
/// backend geometry pools decide how and when to upload it.
class TriangleMesh final : public RenderObject {
    friend class TXContext;

public:
    /// \brief Non-owning representation consumed by device programs.
    struct DeviceImpl;

    /// \brief Returns object-space vertex positions.
    [[nodiscard]] std::span<Vec3f const> getVertices() const noexcept { return vertices_; }

    /// \brief Returns zero-based vertex indices for each triangle.
    [[nodiscard]] std::span<Vec3u const> getTriangles() const noexcept { return triangles_; }

private:
    TriangleMesh(TXContext &tx, kira::Properties properties);
    void loadObj(std::filesystem::path const &path);

    std::vector<Vec3f> vertices_;
    std::vector<Vec3u> triangles_;
};

/// \brief Device implementation of an indexed triangle mesh.
///
/// The pointers refer to storage owned by \c OptixGeometryPool. Member
/// function definitions live in the device-only TriangleMesh header.
struct TriangleMesh::DeviceImpl {
    /// Device array of object-space vertex positions.
    Vec3f const *vertices{};

    /// Device array of zero-based triangle vertex indices.
    Vec3u const *triangles{};

    /// Number of elements in \c vertices.
    std::uint32_t numVertices{};

    /// Number of elements in \c triangles.
    std::uint32_t numTriangles{};

    /// \brief Returns one vertex of \p triangle.
    ///
    /// \pre \p triangle is less than \c numTriangles and \p corner is less
    /// than three.
    [[nodiscard]] KIRA_DEVICE inline Vec3f
    getVertex(std::uint32_t triangle, std::uint32_t corner) const noexcept;

    /// \brief Returns the object-space geometric normal of \p triangle.
    ///
    /// \pre \p triangle is less than \c numTriangles and is non-degenerate.
    [[nodiscard]] KIRA_DEVICE inline Vec3f getFaceNormal(std::uint32_t triangle) const noexcept;
};

static_assert(std::is_standard_layout_v<TriangleMesh::DeviceImpl>);
static_assert(std::is_trivially_copyable_v<TriangleMesh::DeviceImpl>);

namespace optix {
/// Device representation of an indexed triangle mesh.
using TriangleMesh = ::flux::TriangleMesh::DeviceImpl;
} // namespace optix
} // namespace flux
