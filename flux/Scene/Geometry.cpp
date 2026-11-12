#include "flux/Scene/Geometry.h"

#include <string>

#include "flux/Scene/TXContext.h"
#include "flux/Scene/TriangleMesh.h"
#include "kira/Anyhow.h"

namespace flux {
Ref<Geometry> Geometry::create(TXContext &tx, kira::Properties const &props) {
    auto const type = props.use<std::string>("type");
    if (type == "trimesh")
        return tx.create<TriangleMesh>(props);
    throw kira::Anyhow("Geometry: unsupported type '{}'", type);
}

Geometry::Geometry(TXContext &tx, GeometryType type) : RenderObject(tx), type_(type) {}
} // namespace flux
