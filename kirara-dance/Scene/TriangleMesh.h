#pragma once

#include <Eigen/Core>
#include <range/v3/view/single.hpp>

#include "Core/Math.h"
#include "Core/Object.h"
#include "SceneGraph/Group.h"
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
    void loadFromAssimp(
        aiMesh const *inMesh, std::string_view name,
        std::unordered_map<std::string, uint64_t> const &transIdMap
    );
    ///
    void loadFromFile(std::filesystem::path const &path);

public:
    /// Returns the number of vertices.
    [[nodiscard]] auto getNumVertices() const { return V.rows(); }
    /// Returns the number of faces.
    [[nodiscard]] auto getNumFaces() const { return F.rows(); }

    /// Returns true if the mesh has normals.
    [[nodiscard]] bool hasNormals() const { return N.size() != 0; }
    /// Returns true if the mesh has weights.
    [[nodiscard]] bool hasWeights() const { return W.size() != 0; }

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
    ///
    [[nodiscard]] auto const &getWeights() const { return W; }
    ///
    [[nodiscard]] auto &getWeights() { return W; }

    /// \brief Calculates the normals of the mesh.
    ///
    /// If the mesh does not have normals, it will calculate the normals. Else it will discard the
    /// normals and recalculate. It is guaranteed that after calling this function, \c hasNormals()
    /// returns true.
    void calculateNormal(NormalWeightingType weighting = NormalWeightingType::ByAngle);

    /// \brief Adapt the skinned mesh to the root transform.
    Ref<TriangleMesh>
    adaptLinearBlendSkinning(Ref<Transform> const &root, float4x4 const &offset) const;

public:
    ranges::any_view<Ref<Node>> traverse(Visitor &visitor) override {
        return ranges::views::single(children);
    }
    ranges::any_view<Ref<Node>> traverse(ConstVisitor &visitor) const override {
        return ranges::views::single(children);
    }

    ///
    std::string getHumanReadable() const override {
        return fmt::format("[{} ({}): {}]", getTypeName(), getId(), name);
    }

    /// Add a child node to this transform node.
    void addChild(Ref<Node> child) { children->addChild(std::move(child)); }

private:
    /// A string representing the name of the mesh.
    std::string name;

    Eigen::MatrixXf V, N;
    Eigen::MatrixX<uint32_t> F;

    //
    //
    /// \brief A VxB matrix, where V is the number of vertices and B is the number of bones.
    ///
    /// Each row is a vertex, and each column is a weight in the bone.
    /// Each row sums up to 1 or 0.
    ///
    /// This matrix might be empty if the mesh is not skinned.
    Eigen::MatrixXf W;

    /// A vector of size B
    kira::SmallVector<float4x4> inverseBindMatrices;

    // TODO(krr): we might change this to other representation.
    /// A vector of size B
    kira::SmallVector<uint64_t> nodeIds;

    //
    Ref<Group> children{Group::create()};
};
} // namespace krd
