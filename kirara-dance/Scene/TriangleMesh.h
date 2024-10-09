#pragma once

#include <Eigen/Core>

#include "Core/Object.h"
#include "Scene/SceneObject.h"

namespace krd {
/// A triangle mesh in the most general format.
class TriangleMesh final : public SceneObject {
public:
    /// \see igl::PerVertexNormalsWeightingType
    enum class NormalWeightingType : uint8_t {
        ByArea,
        ByAngle,
    };

    /// Constructor.
    explicit TriangleMesh(Scene *scene);

    ///
    [[nodiscard]] static Ref<TriangleMesh> create(Scene *scene) {
        return {new TriangleMesh(scene)};
    }

    ///
    [[nodiscard]] static Ref<TriangleMesh> create(Scene *scene, std::filesystem::path const &path) {
        auto mesh = create(scene);
        mesh->loadFromFile(path);
        return mesh;
    }

    ///
    ~TriangleMesh() override = default;

public:
    void loadFromFile(std::filesystem::path const &path);

public:
    /// Returns the number of vertices.
    [[nodiscard]] auto getNumVertices() const { return V.rows(); }
    /// Returns the number of faces.
    [[nodiscard]] auto getNumFaces() const { return F.rows(); }

    /// Returns true if the mesh has normals.
    [[nodiscard]] bool hasNormals() const { return N.size() != 0; }

    ///
    [[nodiscard]] auto const &getVertices() const { return V; }
    ///
    [[nodiscard]] auto const &getNormals() const { return N; }
    ///
    [[nodiscard]] auto const &getFaces() const { return F; }

    /// \brief Calculates the normals of the mesh.
    ///
    /// If the mesh does not have normals, it will calculate the normals. Else it will discard the
    /// normals and recalculate. It is guaranteed that after calling this function, \c hasNormals()
    /// returns true.
    void calculateNormal(NormalWeightingType weighting = NormalWeightingType::ByAngle);

private:
    Eigen::MatrixXf V, N;
    Eigen::MatrixX<uint32_t> F;
};
} // namespace krd
