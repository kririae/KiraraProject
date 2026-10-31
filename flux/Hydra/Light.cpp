#include "flux/Hydra/Light.h"

#include <pxr/base/gf/color.h>
#include <pxr/base/gf/colorSpace.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <memory>

#include "flux/Core/HostBuffer.h"
#include "flux/Core/MathUtils.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/Transform.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/EDF.h"
#include "kira/Properties.h"

// The Tf diagnostic macros name Tf without its namespace.
PXR_NAMESPACE_USING_DIRECTIVE

namespace {
/// The geometric normal is -Z.
[[nodiscard]] flux::Ref<flux::TriangleMesh>
makeRectangle(flux::Context &context, flux::Vec2f const &size) {
    auto const halfWidth = size.x() * 0.5F;
    auto const halfHeight = size.y() * 0.5F;

    auto vertices = std::make_shared<flux::HostBuffer<flux::Vec3f>>();
    vertices->resize(4, 5);
    vertices->data()[0] = flux::Vec3f{-halfWidth, -halfHeight, 0.0F};
    vertices->data()[1] = flux::Vec3f{-halfWidth, halfHeight, 0.0F};
    vertices->data()[2] = flux::Vec3f{halfWidth, halfHeight, 0.0F};
    vertices->data()[3] = flux::Vec3f{halfWidth, -halfHeight, 0.0F};

    auto triangles = std::make_shared<flux::HostBuffer<flux::Vec3u>>();
    triangles->resize(2);
    triangles->data()[0] = flux::Vec3u{0, 1, 2};
    triangles->data()[1] = flux::Vec3u{0, 2, 3};

    flux::TriangleMesh::Data data;
    data.vertices = std::move(vertices);
    data.triangles = std::move(triangles);
    return context.create<flux::TriangleMesh>(std::move(data));
}

[[nodiscard]] float
worldArea(flux::TriangleMesh const &mesh, std::array<float, 12> const &transform) {
    auto const apply = [&](flux::Vec3f const &vector) {
        return flux::Vec3f{
            transform[0] * vector.x() + transform[1] * vector.y() + transform[2] * vector.z(),
            transform[4] * vector.x() + transform[5] * vector.y() + transform[6] * vector.z(),
            transform[8] * vector.x() + transform[9] * vector.y() + transform[10] * vector.z(),
        };
    };

    auto const vertices = mesh.getVertices();
    auto area = 0.0F;
    for (auto const &triangle : mesh.getTriangles()) {
        auto const &vertex0 = vertices[triangle[0u]];
        auto const edge1 = apply(vertices[triangle[1u]] - vertex0);
        auto const edge2 = apply(vertices[triangle[2u]] - vertex0);
        area += 0.5F * flux::cross(edge1, edge2).norm();
    }
    return area;
}

[[nodiscard]] flux::Spectrum blackbody(float kelvin) {
    static GfColorSpace const rec709{GfColorSpaceNames->LinearRec709};
    static constexpr GfVec3f rec709Luminance{0.2126F, 0.7152F, 0.0722F};

    GfColor color{rec709};
    // The range of UsdLuxBlackbodyTemperatureAsRgb.
    color.SetFromPlanckianLocus(std::clamp(kelvin, 1000.0F, 10000.0F), 1.0F);
    auto const rgb = color.GetRGB() / GfDot(color.GetRGB(), rec709Luminance);
    return {rgb[0], rgb[1], rgb[2]};
}

template <typename T>
[[nodiscard]] T readParam(
    HdSceneDelegate &sceneDelegate, SdfPath const &id, TfToken const &token, T const &fallback
) {
    return sceneDelegate.GetLightParamValue(id, token).GetWithDefault(fallback);
}

[[nodiscard]] flux::Vec2f readSize(HdSceneDelegate &sceneDelegate, SdfPath const &id) {
    return {
        std::abs(readParam(sceneDelegate, id, HdLightTokens->width, 1.0F)),
        std::abs(readParam(sceneDelegate, id, HdLightTokens->height, 1.0F)),
    };
}

[[nodiscard]] flux::Spectrum
readRadiance(HdSceneDelegate &sceneDelegate, SdfPath const &id, float area) {
    auto const read = [&]<typename T>(TfToken const &token, T const &fallback) {
        return readParam(sceneDelegate, id, token, fallback);
    };

    auto const color = read(HdLightTokens->color, GfVec3f{1.0F});
    auto const scale =
        read(HdLightTokens->intensity, 1.0F) * std::exp2(read(HdLightTokens->exposure, 0.0F));
    auto radiance = flux::Spectrum{color[0], color[1], color[2]} * scale;

    if (read(HdLightTokens->enableColorTemperature, false))
        radiance = radiance * blackbody(read(HdLightTokens->colorTemperature, 6500.0F));
    if (read(HdLightTokens->normalize, false) && area > 0.0F)
        radiance = radiance / area;
    return radiance;
}
} // namespace

HdFluxLight::HdFluxLight(SdfPath const &id) : HdLight(id) {}

HdFluxLight::~HdFluxLight() = default;

void HdFluxLight::Sync(
    HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits
) {
    auto const &id = GetId();
    auto &param = *static_cast<HdFluxRenderParam *>(renderParam);

    try {
        auto const context = param.GetContext();

        auto const size = readSize(*sceneDelegate, id);
        if (!primitive_)
            build(*context, size);
        else if (size != size_)
            primitive_->setGeometry(makeRectangle(*context, size));
        size_ = size;

        primitive_->setVisible(sceneDelegate->GetVisible(id));
        primitive_->setTransform(HdFluxToTransform(sceneDelegate->GetTransform(id)));

        auto const mesh = primitive_->getGeometry().dynamicCast<flux::TriangleMesh const>();
        auto const area = worldArea(*mesh, primitive_->getTransform());
        edf_->setRadiance(readRadiance(*sceneDelegate, id, area));
    } catch (std::exception const &error) { TF_WARN("%s: %s", id.GetText(), error.what()); }

    *dirtyBits &= ~AllDirty;
}

HdDirtyBits HdFluxLight::GetInitialDirtyBitsMask() const { return AllDirty; }

void HdFluxLight::Finalize(HdRenderParam *renderParam) {
    if (!primitive_)
        return;

    auto const context = static_cast<HdFluxRenderParam *>(renderParam)->GetContext();
    context->remove(primitive_->getContextId());
}

void HdFluxLight::build(flux::Context &context, flux::Vec2f const &size) {
    edf_ = context.create<flux::ConstantEDF>(kira::Properties{});

    auto primitive = context.create<flux::Primitive>(makeRectangle(context, size));
    primitive->setEDF(edf_);
    primitive_ = std::move(primitive);
}
