#pragma once

#include "Transform.h"
#include "TriangleMesh.h"

namespace krd {
/// \brief A geometry node in the scene graph.
///
/// Geometry is a instance of a mesh. It can be transformed and linked to a mesh.
/// It can be used to create a hierarchy of meshes in the scene graph.
class Geometry : public NodeMixin<Geometry, Transform> {
public:
    [[nodiscard]] static Ref<Geometry> create() { return {new Geometry}; }

public:
    /// \brief Link a mesh's lifetime to this geometry.
    ///
    /// \remark Only one mesh can be linked to a geometry for now.
    /// \remark traverse() will not be linked to the mesh.
    void linkMesh(Ref<TriangleMesh> mesh) { this->mesh = std::move(mesh); }
    /// Get the mesh linked to this geometry.
    Ref<TriangleMesh> getMesh() const { return mesh; }

    // debug
    void linkDynamicMesh(Ref<TriangleMesh> mesh) { this->dynMesh = std::move(mesh); }
    Ref<TriangleMesh> getDynamicMesh() const { return dynMesh; }

private:
    // NOTE(krr): no traversable reference is established to this mesh instance.
    Ref<TriangleMesh> mesh;

    //
    Ref<TriangleMesh> dynMesh;
};
} // namespace krd
