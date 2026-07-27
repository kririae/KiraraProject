#include "flux/Scene/Primitive.h"

#include <cstdint>
#include <limits>
#include <utility>

#include "flux/Scene/Context.h"
#include "flux/Scene/TriangleMesh.h"
#include "kira/Anyhow.h"

namespace flux {
Primitive::Primitive(TXContext &tx, kira::Properties properties)
    : RenderObject(tx, std::move(properties)) {
    auto const geometry = getProperties().use<std::int64_t>("geometry_ctx_id");
    if (geometry < 0 || static_cast<std::uint64_t>(geometry) >
                            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        throw kira::Anyhow("Primitive: geometry ID is out of range");
    geometryContextId_ = static_cast<std::size_t>(geometry);
}

Ref<TriangleMesh const> Primitive::getGeometry() const {
    auto const *context = getContext();
    if (!context)
        throw kira::Anyhow("Primitive: owning context no longer exists");
    return context->get<TriangleMesh>(geometryContextId_);
}

void Primitive::link() { (void)getGeometry(); }
} // namespace flux
