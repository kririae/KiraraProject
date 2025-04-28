#pragma once

#include <Eigen/Core>

#include "Core/Object.h"
#include "SceneGraph/Node.h"
#include "SceneGraph/NodeMixin.h"

struct aiMesh;

namespace krd {
/// A triangle mesh in the most general format.
class TriangleMesh final : public NodeMixin<TriangleMesh, Node> {
public:
    /// \see igl::PerVertexNormalsWeightingType
    enum class NormalWeightingType : uint8_t {
        ByArea,
        ByAngle,
    };

    ///
    [[nodiscard]] static Ref<TriangleMesh> create() { return {new TriangleMesh}; }

    ///
    ~TriangleMesh() override = default;

public:
    ///
    void loadFromAssimp(aiMesh const *inMesh, std::string_view name);
    ///
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
    [[nodiscard]] auto &getVertices() { return V; }
    ///
    [[nodiscard]] auto const &getNormals() const { return N; }
    ///
    [[nodiscard]] auto &getNormals() { return N; }
    ///
    [[nodiscard]] auto const &getFaces() const { return F; }
    ///
    [[nodiscard]] auto &getFaces() { return F; }

#if 0
    /// \brief Get the local transformation of the mesh.
    ///
    /// If the \c TriangleMesh is attached to a \c SceneNode, it will return the transformation of
    /// the node. Otherwise, it will return the identity transformation.
    [[nodiscard]] Transform getLocalTransform() const;

    /// \brief Get the global transformation of the mesh.
    ///
    /// If the \c TriangleMesh is attached to a \c SceneNode, it will return the transformation of
    /// the node. Otherwise, it will return the identity transformation.
    ///
    /// \see Transform::getGlobalTransformMatrix
    [[nodiscard]] float4x4 getGlobalTransformMatrix() const;
#endif

    /// \brief Get the global transformation of the mesh

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
