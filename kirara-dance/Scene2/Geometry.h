#pragma once

#include "Transform.h"
#include "TriangleMesh.h"

namespace krd {
/// \brief A geometry node in the scene graph.
///
/// Geometry is a instance of a mesh. It can be transformed and linked to a mesh.
/// It can be used to create a hierarchy of meshes in the scene graph.
/// It is a subclass of Transform, which means it can be transformed.
class Geometry : public Transform {
public:
    [[nodiscard]] static Ref<Geometry> create() { return {new Geometry}; }
    ~Geometry() override = default;

    void accept(Visitor &visitor) override { visitor.apply(*this); }
    void accept(ConstVisitor &visitor) const override { visitor.apply(*this); }

    /// \brief Link a mesh's lifetime to this geometry.
    ///
    /// \remark Only one mesh can be linked to a geometry for now.
    /// \remark traverse() will not be linked to the mesh.
    void linkMesh(Ref<TriangleMesh> mesh) { this->mesh = std::move(mesh); }

    /// Get the mesh linked to this geometry.
    Ref<TriangleMesh> getMesh() const { return mesh; }

private:
    Ref<TriangleMesh> mesh;
};
} // namespace krd
