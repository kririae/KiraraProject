#pragma once

#include <filesystem>
#include <span>
#include <vector>

#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"

namespace flux {
/// \brief Host-side indexed triangle mesh.
///
/// The \c path property names an OBJ file. This class owns CPU data only;
/// backend geometry pools decide how and when to upload it.
class TriangleMesh final : public RenderObject {
    friend class TXContext;

public:
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
} // namespace flux
