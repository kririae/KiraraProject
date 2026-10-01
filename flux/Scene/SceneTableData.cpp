#include "flux/Scene/SceneTableData.h"

#include <cstddef>
#include <limits>
#include <unordered_map>
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
    objects.meshes.reserve(contextPrimitives.size());
    primitives.reserve(contextPrimitives.size());
    transforms.reserve(contextPrimitives.size());

    // Unused indices hold the first implementation, so every index below a limit
    // addresses valid storage.
    if (!contextBSDFs.empty()) {
        bsdfs.assign(context.getBSDFIndexLimit(), contextBSDFs.front()->getImpl());
        for (auto const &bsdf : contextBSDFs)
            bsdfs[context.getBSDFIndex(bsdf->getContextId())] = bsdf->getImpl();
    }
    if (!contextEDFs.empty()) {
        edfs.assign(context.getEDFIndexLimit(), contextEDFs.front()->getImpl());
        for (auto const &edf : contextEDFs)
            edfs[context.getEDFIndex(edf->getContextId())] = edf->getImpl();
    }

    // Shared geometries use one dense geometry index.
    std::unordered_map<std::size_t, std::uint32_t> geometryIndexByContextId;
    geometryIndexByContextId.reserve(contextPrimitives.size());

    auto const getOrAddGeometryIndex = [&](Ref<Geometry const> const &geometry) {
        auto const contextId = geometry->getContextId();
        if (auto const iterator = geometryIndexByContextId.find(contextId);
            iterator != geometryIndexByContextId.end())
            return iterator->second;

        if (objects.meshes.size() >= std::numeric_limits<std::uint32_t>::max())
            throw kira::Anyhow("SceneTableData: geometry count exceeds the dense index range");

        auto const index = static_cast<std::uint32_t>(objects.meshes.size());
        // Record the mesh at its dense geometry index.
        switch (geometry->getType()) {
        case GeometryType::TriangleMesh: {
            auto mesh = geometry.dynamicCast<TriangleMesh const>();
            if (!mesh)
                throw kira::Anyhow(
                    "SceneTableData: geometry type does not match its host implementation"
                );
            objects.meshes.push_back(std::move(mesh));
            break;
        }
        case GeometryType::Count: throw kira::Anyhow("SceneTableData: unsupported geometry type");
        }

        geometryIndexByContextId.emplace(contextId, index);
        return index;
    };

    for (auto const &primitive : contextPrimitives) {
        if (!primitive->isVisible())
            continue;
        if (primitives.size() >= std::numeric_limits<std::uint32_t>::max())
            throw kira::Anyhow("SceneTableData: primitive count exceeds the dense index range");

        // Resolve the dense geometry index.
        auto const geometryIndex = getOrAddGeometryIndex(primitive->getGeometry());

        // Resolve the BSDF index assigned by Context.
        auto const bsdf = primitive->getBSDF();
        auto bsdfIndex = Primitive::Impl::invalidBSDFIndex;
        if (bsdf)
            bsdfIndex = context.getBSDFIndex(bsdf->getContextId());

        // Resolve the EDF index assigned by Context.
        auto const edf = primitive->getEDF();
        auto edfIndex = Primitive::Impl::invalidEDFIndex;
        if (edf)
            edfIndex = context.getEDFIndex(edf->getContextId());

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
