#include "flux/Scene/SceneTableData.h"

#include <array>
#include <cstddef>

#include "flux/Scene/Context.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"

namespace flux {
void SceneTableData::build(Context const &context) {
    clear();

    auto const contextPrimitives = context.getObjects<Primitive>();
    auto const contextBSDFs = context.getObjects<BSDF>();
    auto const contextEDFs = context.getObjects<EDF>();

    // Fill each table with empty entries, then place each object's entry at its index.
    primitives.assign(context.getIndexLimit<Primitive>(), Primitive::Impl{});
    transforms.assign(context.getIndexLimit<Primitive>(), std::array<float, 12>{});
    for (auto const &primitive : contextPrimitives) {
        auto const index = context.getIndex<Primitive>(primitive->getContextId());
        primitives[index] = primitive->getImpl();
        if (primitive->isVisible())
            transforms[index] = primitive->getTransform();
    }

    bsdfs.assign(context.getIndexLimit<BSDF>(), BSDF::Impl{});
    for (auto const &bsdf : contextBSDFs)
        bsdfs[context.getIndex<BSDF>(bsdf->getContextId())] = bsdf->getImpl();

    edfs.assign(context.getIndexLimit<EDF>(), EDF::Impl{});
    for (auto const &edf : contextEDFs)
        edfs[context.getIndex<EDF>(edf->getContextId())] = edf->getImpl();
}

void SceneTableData::clear() noexcept {
    primitives.clear();
    transforms.clear();
    bsdfs.clear();
    edfs.clear();
}
} // namespace flux
