#include "flux/Scene/SceneTableData.h"

#include <array>
#include <cstddef>

#include "flux/Scene/Context.h"
#include "flux/Scene/Geometry.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

namespace flux {
void SceneTableData::build(Context const &context) {
    clear();

    auto const contextPrimitives = context.getObjects<Primitive>();
    auto const contextBSDFs = context.getObjects<BSDF>();
    auto const contextEDFs = context.getObjects<EDF>();

    // Fill each table with empty entries, then place each object's entry at its index.
    objects.primitives.resize(context.getIndexLimit<Primitive>());
    primitives.assign(context.getIndexLimit<Primitive>(), Primitive::Impl{});
    transforms.assign(context.getIndexLimit<Primitive>(), std::array<float, 12>{});
    for (auto const &primitive : contextPrimitives) {
        auto const index = context.getIndex<Primitive>(primitive->getContextId());
        primitives[index] = primitive->getImpl();
        if (primitive->isVisible()) {
            objects.primitives[index] = primitive;
            transforms[index] = primitive->getTransform();
        }
    }

    bsdfs.assign(context.getIndexLimit<BSDF>(), BSDF::Impl{});
    for (auto const &bsdf : contextBSDFs)
        bsdfs[context.getIndex<BSDF>(bsdf->getContextId())] = bsdf->getImpl();

    edfs.assign(context.getIndexLimit<EDF>(), EDF::Impl{});
    for (auto const &edf : contextEDFs)
        edfs[context.getIndex<EDF>(edf->getContextId())] = edf->getImpl();

    // Derive the geometry holes from the primitives. No geometry can tell alone
    // whether a visible primitive references it, so this pass is not per object.
    objects.meshes.resize(context.getIndexLimit<Geometry>());
    for (auto const &primitive : contextPrimitives) {
        if (!primitive->isVisible())
            continue;

        auto const geometry = primitive->getGeometry();
        auto &mesh = objects.meshes[context.getIndex<Geometry>(geometry->getContextId())];
        if (mesh)
            continue;

        switch (geometry->getType()) {
        case GeometryType::TriangleMesh: {
            mesh = geometry.dynamicCast<TriangleMesh const>();
            if (!mesh)
                throw kira::Anyhow(
                    "SceneTableData: geometry type does not match its host implementation"
                );
            break;
        }
        case GeometryType::Count: throw kira::Anyhow("SceneTableData: unsupported geometry type");
        }
    }
}

void SceneTableData::clear() noexcept {
    objects.primitives.clear();
    objects.meshes.clear();
    primitives.clear();
    transforms.clear();
    bsdfs.clear();
    edfs.clear();
}
} // namespace flux
