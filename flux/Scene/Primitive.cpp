#include "flux/Scene/Primitive.h"

#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include "flux/Scene/Context.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] std::size_t readContextId(kira::Properties const &properties, std::string_view name) {
    auto const value = properties.use<std::int64_t>(name);
    if (value < 0 || static_cast<std::uint64_t>(value) >
                         static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        throw kira::Anyhow("Primitive: {} is out of range", name);
    return static_cast<std::size_t>(value);
}
} // namespace

Primitive::Primitive(TXContext &tx, kira::Properties properties)
    : RenderObject(tx, std::move(properties)) {
    geometryContextId_ = readContextId(getProperties(), "geometry_ctx_id");
    if (getProperties().contains("bsdf_ctx_id"))
        bsdfContextId_ = readContextId(getProperties(), "bsdf_ctx_id");
}

Ref<TriangleMesh const> Primitive::getGeometry() const {
    auto const *context = getContext();
    if (!context)
        throw kira::Anyhow("Primitive: owning context no longer exists");
    return context->get<TriangleMesh>(geometryContextId_);
}

Ref<BSDF const> Primitive::getBSDF() const {
    if (!bsdfContextId_)
        return {};

    auto const *context = getContext();
    if (!context)
        throw kira::Anyhow("Primitive: owning context no longer exists");
    return context->get<BSDF>(*bsdfContextId_);
}

void Primitive::link() {
    (void)getGeometry();
    (void)getBSDF();
}
} // namespace flux
