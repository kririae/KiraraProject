#pragma once

#include <Eigen/Core>
#include <range/v3/view/single.hpp>

#include "Core/Math.h"
#include "Core/Object.h"
#include "SceneGraph/Group.h"
#include "SceneGraph/NodeMixins.h"

struct aiMesh;

namespace krd {
/// A triangle mesh in the most general format.
class TriangleMesh final : public SerializableMixin<TriangleMesh, Group> {
public:
    /// \see igl::PerVertexNormalsWeightingType
    enum class NormalWeightingType : uint8_t {
        ByArea,
        ByAngle,
    };

    ///
    [[nodiscard]] static Ref<TriangleMesh> create() { return {new TriangleMesh}; }

public:
    ///
    void loadFromAssimp(aiMesh const *inMesh, std::string_view name);
    ///
    void loadFromAssimp(
        aiMesh const *inMesh, std::string_view name,
        std::unordered_map<std::string, Node::UUIDType> const &transIdMap
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
    Ref<TriangleMesh> adaptLinearBlendSkinning(Ref<Node> const &root) const;

public:
    ///
    std::string getHumanReadable() const override {
        if (name.empty())
            return fmt::format("[{} ({})]", getTypeName(), getId());
        return fmt::format("[{} ({}): '{}']", getTypeName(), getId(), name);
    }

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
    kira::SmallVector<Node::UUIDType> nodeIds;
    /// A vector of size B
    kira::SmallVector<Node::UUIDType> rootNodeIds;

public:
    void archive(auto &ar) {
        ar(cereal::make_nvp("name", name));
        ar(cereal::make_nvp("V", V));
        ar(cereal::make_nvp("N", N));
        ar(cereal::make_nvp("F", F));
        ar(cereal::make_nvp("W", W));
        ar(cereal::make_nvp("inverseBindMatrices", inverseBindMatrices));
        ar(cereal::make_nvp("nodeIds", nodeIds));
        ar(cereal::make_nvp("rootNodeIds", rootNodeIds));
    }
};
} // namespace krd

// Define the serialization functions for the TriangleMesh subtypes
namespace cereal {
template <class Archive, class Derived>
void save(Archive &ar, Eigen::PlainObjectBase<Derived> const &m)
    requires traits::is_output_serializable<BinaryData<typename Derived::Scalar>, Archive>::value
{
    using ArrT = Eigen::PlainObjectBase<Derived>;
    if (ArrT::RowsAtCompileTime == Eigen::Dynamic)
        ar(m.rows());
    if (ArrT::ColsAtCompileTime == Eigen::Dynamic)
        ar(m.cols());
    ar(binary_data(m.data(), m.size() * sizeof(typename Derived::Scalar)));
}

template <class Archive, class Derived>
void load(Archive &ar, Eigen::PlainObjectBase<Derived> &m)
    requires traits::is_input_serializable<BinaryData<typename Derived::Scalar>, Archive>::value
{
    using ArrT = Eigen::PlainObjectBase<Derived>;
    Eigen::Index rows = ArrT::RowsAtCompileTime, cols = ArrT::ColsAtCompileTime;
    if (rows == Eigen::Dynamic)
        ar(rows);
    if (cols == Eigen::Dynamic)
        ar(cols);
    m.resize(rows, cols);
    ar(binary_data(
        m.data(), static_cast<std::size_t>(rows * cols * sizeof(typename Derived::Scalar))
    ));
}

template <class T>
void save(auto &ar, kira::SmallVector<T> const &vec)
    requires(traits::is_output_serializable<BinaryData<T>, decltype(ar)>::value)
{
    ar(cereal::make_size_tag(vec.size()));
    for (auto const &v : vec)
        ar(v);
}

template <class T>
void load(auto &ar, kira::SmallVector<T> &vec)
    requires(traits::is_input_serializable<BinaryData<T>, decltype(ar)>::value)
{
    size_t size{0};
    ar(cereal::make_size_tag(size));
    vec.resize(size);
    for (auto &v : vec)
        ar(v);
}

void save(auto &ar, ::krd::Node::UUIDType const &id) { ar(binary_data(&id, sizeof(id))); }
void load(auto &ar, ::krd::Node::UUIDType &id) { ar(binary_data(&id, sizeof(id))); }
} // namespace cereal
