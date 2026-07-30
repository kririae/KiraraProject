#include "flux/Scene/Primitive.h"

#include <cstddef>
#include <string>
#include <utility>

#include "flux/Scene/Geometry.h"
#include "flux/Scene/TXContext.h"
#include "flux/Shading/BSDF.h"
#include "kira/Anyhow.h"

namespace flux {
Primitive::Primitive(TXContext &tx, kira::Properties const &props) : RenderObject(tx) {
    if (props.contains("geometry_ctx_id")) {
        auto const contextId = static_cast<std::size_t>(props.use<std::int64_t>("geometry_ctx_id"));
        geometry_ = tx.get<Geometry>(contextId);
    } else {
        geometry_ = tx.create<Geometry>(props);
    }

    if (props.contains("bsdf_ctx_id"))
        bsdf_ = tx.get<BSDF>(static_cast<std::size_t>(props.use<std::int64_t>("bsdf_ctx_id")));
    else if (props.is_type_of<kira::Properties>("bsdf"))
        bsdf_ = tx.create<BSDF>(props.use_view("bsdf"));
    else if (props.is_type_of<std::string>("bsdf"))
        throw kira::Anyhow("Primitive: named BSDF must be resolved before creation");
    else if (props.contains("bsdf"))
        throw kira::Anyhow("Primitive: bsdf must be an inline table");
}

Primitive::~Primitive() = default;

Ref<Geometry const> Primitive::getGeometry() const noexcept { return geometry_; }

void Primitive::setGeometry(Ref<Geometry const> geometry) {
    if (!geometry)
        throw kira::Anyhow("Primitive: geometry must not be null");
    if (!getContext() || geometry->getContext() != getContext())
        throw kira::Anyhow("Primitive: geometry belongs to another context");
    if (geometry_ == geometry)
        return;
    geometry_ = std::move(geometry);
}

Ref<BSDF const> Primitive::getBSDF() const noexcept { return bsdf_; }

void Primitive::setBSDF(Ref<BSDF const> bsdf) {
    if (bsdf && (!getContext() || bsdf->getContext() != getContext()))
        throw kira::Anyhow("Primitive: BSDF belongs to another context");
    if (bsdf_ == bsdf)
        return;
    bsdf_ = std::move(bsdf);
}
} // namespace flux
