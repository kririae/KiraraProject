#include "flux/Scene/Primitive.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <utility>

#include "flux/Scene/Context.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/TXContext.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] Vec3f getScale(std::array<float, 12> const &transform) noexcept {
    auto const length = [&](std::size_t column) {
        return std::sqrt(
            transform[column] * transform[column] + transform[column + 4] * transform[column + 4] +
            transform[column + 8] * transform[column + 8]
        );
    };
    return {length(0), length(1), length(2)};
}
} // namespace

Primitive::Primitive(TXContext &tx, kira::Properties const &props) : RenderObject(tx) {
    if (props.contains("transform")) {
        auto const values = props.use_array_view("transform");
        if (values.size() != transform_.size())
            throw kira::Anyhow("Primitive: transform requires 12 row-major matrix elements");
        for (std::size_t index = 0; index < transform_.size(); ++index)
            transform_[index] = values.get<float>(index);
    }

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

    if (props.contains("edf_ctx_id"))
        edf_ = tx.get<EDF>(static_cast<std::size_t>(props.use<std::int64_t>("edf_ctx_id")));
    else if (props.is_type_of<kira::Properties>("edf"))
        edf_ = tx.create<EDF>(props.use_view("edf"));
    else if (props.is_type_of<std::string>("edf"))
        throw kira::Anyhow("Primitive: named EDF must be resolved before creation");
    else if (props.contains("edf"))
        throw kira::Anyhow("Primitive: edf must be an inline table");
}

Primitive::~Primitive() = default;

Ref<Geometry const> Primitive::getGeometry() const noexcept { return geometry_; }

void Primitive::setGeometry(Ref<Geometry const> geometry) {
    if (!geometry)
        throw kira::Anyhow("Primitive: geometry must not be null");
    if (!getContext() || geometry->getContext() != getContext())
        throw kira::Anyhow("Primitive: geometry belongs to another context");
    setIfDifferent(geometry_, geometry, DirtyBits::Geometry);
}

Ref<BSDF const> Primitive::getBSDF() const noexcept { return bsdf_; }

void Primitive::setBSDF(Ref<BSDF const> bsdf) {
    if (bsdf && (!getContext() || bsdf->getContext() != getContext()))
        throw kira::Anyhow("Primitive: BSDF belongs to another context");
    setIfDifferent(bsdf_, bsdf, DirtyBits::BSDF);
}

Ref<EDF const> Primitive::getEDF() const noexcept { return edf_; }

void Primitive::setEDF(Ref<EDF const> edf) {
    if (edf && (!getContext() || edf->getContext() != getContext()))
        throw kira::Anyhow("Primitive: EDF belongs to another context");
    setIfDifferent(edf_, edf, DirtyBits::EDF);
}

Primitive::Impl Primitive::getImpl() const {
    if (!visible_)
        return {};

    auto const &context = *getContext();
    return {
        .geometryIndex = context.getIndex<Geometry>(geometry_->getContextId()),
        .bsdfIndex = bsdf_ ? context.getIndex<BSDF>(bsdf_->getContextId()) : Impl::invalidBSDFIndex,
        .edfIndex = edf_ ? context.getIndex<EDF>(edf_->getContextId()) : Impl::invalidEDFIndex,
    };
}

float Primitive::estimateAreaScale() const noexcept {
    auto const scale = getScale(transform_);
    return (scale.x() * scale.y() + scale.x() * scale.z() + scale.y() * scale.z()) / 3.0F;
}

bool Primitive::hasNonUniformScale() const noexcept {
    auto const scale = getScale(transform_);
    auto const maximum = scale.hmax();
    auto const minimum = std::min(scale.x(), std::min(scale.y(), scale.z()));
    return maximum > 0.0F && maximum - minimum > maximum * 1.0e-4F;
}

float Primitive::estimatePower() const noexcept {
    if (!edf_)
        return 0.0F;
    return std::numbers::pi_v<float> * geometry_->getSurfaceArea() * estimateAreaScale() *
           edf_->estimateLuminance();
}
} // namespace flux
