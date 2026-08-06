#include <gtest/gtest.h>

#include <array>
#include <stdexcept>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Scene/TXContext.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

namespace {
class ThrowingIntegratorOwner final : public flux::RenderObject {
    friend class flux::TXContext;

    ThrowingIntegratorOwner(flux::TXContext &tx, kira::Properties const &) : RenderObject(tx) {
        (void)tx.create<flux::PathIntegrator>();
        throw std::runtime_error("intentional integrator transaction failure");
    }
};

struct EmitterHitContext {
    flux::Geometry::Impl geometry;
    flux::Primitive::Impl primitive;
    flux::EDF::Impl edf;
    flux::LightSampler lightSampler;

    [[nodiscard]] flux::Geometry::Impl const &getGeometry(std::uint32_t) const noexcept {
        return geometry;
    }
    [[nodiscard]] flux::Primitive::Impl const &getPrimitive(std::uint32_t) const noexcept {
        return primitive;
    }
    [[nodiscard]] flux::EDF::Impl const &getEDF(std::uint32_t) const noexcept { return edf; }
    [[nodiscard]] flux::LightSampler const &getLightSampler() const noexcept {
        return lightSampler;
    }
};
} // namespace

TEST(PathIntegratorTests, KeepsFirstSuccessfulIntegratorActive) {
    auto context = flux::Context::create();

    EXPECT_THROW((void)context->getActiveIntegrator(), kira::Anyhow);
    EXPECT_THROW((void)context->create<ThrowingIntegratorOwner>(), std::runtime_error);
    EXPECT_THROW((void)context->getActiveIntegrator(), kira::Anyhow);

    auto first = context->create<flux::PathIntegrator>();
    EXPECT_EQ(context->getActiveIntegrator().get(), first.get());
    EXPECT_TRUE(first->usesShaderReorder());

    (void)context->create<flux::PathIntegrator>();
    EXPECT_EQ(context->getActiveIntegrator().get(), first.get());
}

TEST(PathIntegratorTests, DisablesShaderReorderingFromProperties) {
    auto context = flux::Context::create();
    kira::Properties props;
    props.set("shader_reorder", false);

    auto integrator = context->create<flux::PathIntegrator>(props);

    EXPECT_FALSE(integrator->usesShaderReorder());
}

TEST(PathIntegratorTests, WeightsBsdfSampledEmitterHits) {
    auto const record = flux::LightRecord{.type = flux::LightRecordType::Primitive};
    auto const primitiveIndex = std::uint32_t{0};
    auto const areaScale = 1.0F;
    auto const powerCDF = 1.0F;
    auto const vertices = std::array{
        flux::Vec3f{0.0F, 0.0F, 0.0F},
        flux::Vec3f{4.0F, 0.0F, 0.0F},
        flux::Vec3f{0.0F, 1.0F, 0.0F},
    };
    auto const triangles = std::array{flux::Vec3u{0, 1, 2}};
    auto const triangleAreaCDF = std::array{2.0F};
    auto const triangleAreaPDF = std::array{0.5F};
    auto const context = EmitterHitContext{
        .geometry =
            flux::TriangleMesh::Impl{
                .vertices = vertices.data(),
                .triangles = triangles.data(),
                .numVertices = static_cast<std::uint32_t>(vertices.size()),
                .numTriangles = static_cast<std::uint32_t>(triangles.size()),
                .triangleAreaCDF = triangleAreaCDF.data(),
                .triangleAreaPDF = triangleAreaPDF.data(),
                .surfaceArea = triangleAreaCDF.back(),
            },
        .primitive =
            {
                .geometryIndex = 0,
                .edfIndex = 0,
                .lightIndex = 0,
            },
        .edf = flux::ConstantEDF::Impl{.radiance = {2.0F, 2.0F, 2.0F}},
        .lightSampler = {
            .lights = {
                .records = &record,
                .primitiveIndices = &primitiveIndex,
                .primitiveAreaScales = &areaScale,
                .powerCDF = &powerCDF,
                .powerSum = powerCDF,
                .numLights = 1,
            },
        },
    };
    auto state = flux::PathState{
        .prevLightCtx = {.position = {0.0F, 0.0F, 0.0F}},
        .prevBSDFPdf = 0.5F,
        .depth = 1,
    };
    auto const surface = flux::SurfaceInteraction{
        .position = {0.0F, 0.0F, 1.0F},
        .geometricNormal = {0.0F, 0.0F, 1.0F},
    };

    flux::PathIntegrator::Impl{8, 2, 0.95F}.onEmitterHit(
        state, context, context.primitive, surface, {0.0F, 0.0F, 1.0F}
    );

    EXPECT_EQ(state.radiance, (flux::Spectrum{1.0F, 1.0F, 1.0F}));
}
