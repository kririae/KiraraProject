#include "flux/Scene/Primitive.h"

#include <Eigen/Core>
#include <Eigen/LU>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <utility>

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

Ref<EDF const> Primitive::getEDF() const noexcept { return edf_; }

void Primitive::setEDF(Ref<EDF const> edf) {
    if (edf && (!getContext() || edf->getContext() != getContext()))
        throw kira::Anyhow("Primitive: EDF belongs to another context");
    if (edf_ == edf)
        return;
    edf_ = std::move(edf);
}

std::array<float, 9> Primitive::getNormalTransform() const {
    using AffineTransform = Eigen::Matrix<float, 3, 4, Eigen::RowMajor>;
    using NormalMatrix = Eigen::Matrix<float, 3, 3, Eigen::RowMajor>;

    Eigen::Map<AffineTransform const> objectToWorld(transform_.data());
    Eigen::Matrix3f const linear = objectToWorld.leftCols<3>();
    if (linear.determinant() == 0.0F)
        throw kira::Anyhow("Primitive: transform is singular");

    std::array<float, 9> result{};
    Eigen::Map<NormalMatrix> normalTransform(result.data());
    normalTransform = linear.inverse().transpose();
    return result;
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
