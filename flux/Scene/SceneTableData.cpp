#include "flux/Scene/SceneTableData.h"

#include <cstddef>
#include <limits>
#include <utility>

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
    objects.primitives.reserve(contextPrimitives.size());
    primitives.reserve(contextPrimitives.size());
    transforms.reserve(contextPrimitives.size());

    // Fill the tables with empty entries, then place each object at its index.
    bsdfs.assign(context.getIndexLimit<BSDF>(), BSDF::Impl{});
    for (auto const &bsdf : contextBSDFs)
        bsdfs[context.getIndex<BSDF>(bsdf->getContextId())] = bsdf->getImpl();
    edfs.assign(context.getIndexLimit<EDF>(), EDF::Impl{});
    for (auto const &edf : contextEDFs)
        edfs[context.getIndex<EDF>(edf->getContextId())] = edf->getImpl();

    // Leave a hole at every geometry index until a visible primitive references it.
    objects.meshes.resize(context.getIndexLimit<Geometry>());

    auto const placeMesh = [&](Ref<Geometry const> const &geometry) {
        auto const index = context.getIndex<Geometry>(geometry->getContextId());
        if (objects.meshes[index])
            return index;

        switch (geometry->getType()) {
        case GeometryType::TriangleMesh: {
            auto mesh = geometry.dynamicCast<TriangleMesh const>();
            if (!mesh)
                throw kira::Anyhow(
                    "SceneTableData: geometry type does not match its host implementation"
                );
            objects.meshes[index] = std::move(mesh);
            break;
        }
        case GeometryType::Count: throw kira::Anyhow("SceneTableData: unsupported geometry type");
        }
        return index;
    };

    for (auto const &primitive : contextPrimitives) {
        if (!primitive->isVisible())
            continue;
        if (primitives.size() >= std::numeric_limits<std::uint32_t>::max())
            throw kira::Anyhow("SceneTableData: primitive count exceeds the dense index range");

        // Resolve the geometry index assigned by Context.
        auto const geometryIndex = placeMesh(primitive->getGeometry());

        // Resolve the BSDF index assigned by Context.
        auto const bsdf = primitive->getBSDF();
        auto bsdfIndex = Primitive::Impl::invalidBSDFIndex;
        if (bsdf)
            bsdfIndex = context.getIndex<BSDF>(bsdf->getContextId());

        // Resolve the EDF index assigned by Context.
        auto const edf = primitive->getEDF();
        auto edfIndex = Primitive::Impl::invalidEDFIndex;
        if (edf)
            edfIndex = context.getIndex<EDF>(edf->getContextId());

        // Write the primitive at its dense index in every segment.
        objects.primitives.push_back(primitive);
        primitives.push_back({
            .geometryIndex = geometryIndex,
            .bsdfIndex = bsdfIndex,
            .edfIndex = edfIndex,
        });
        transforms.push_back(primitive->getTransform());
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
