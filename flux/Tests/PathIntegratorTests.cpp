#include <gtest/gtest.h>

#include <array>
#include <numbers>
#include <stdexcept>

#include "flux/Core/RayFootprint.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Scene/Context.h"
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
    flux::EDF::Impl edf;

    [[nodiscard]] flux::EDF::Impl const &getEDF(std::uint32_t) const noexcept { return edf; }

    [[nodiscard]] float pdfDirectLight(
        flux::LightSamplingContext const &, flux::Primitive::Impl const &,
        flux::SurfaceInteraction const &
    ) const noexcept {
        return 0.5F;
    }
};
} // namespace

TEST(PathIntegratorTests, PropagatesAndPacksRayFootprints) {
    auto footprint = flux::RayFootprint{
        .cones = {
            flux::RayCone{.angle = 0.1F, .width = 0.2F},
            flux::RayCone{.angle = -0.1F, .width = 0.05F},
        },
    };
    footprint.propagate(1.0F);

    EXPECT_FLOAT_EQ(footprint.cones[0].angle, 0.1F);
    EXPECT_FLOAT_EQ(footprint.cones[0].width, 0.3F);
    EXPECT_FLOAT_EQ(footprint.cones[1].angle, 0.1F);
    EXPECT_FLOAT_EQ(footprint.cones[1].width, 0.05F);

    auto const unpacked = flux::PackedRayFootprint::pack(footprint).unpack();
    EXPECT_NEAR(unpacked.cones[0].angle, footprint.cones[0].angle, 1.0e-3F);
    EXPECT_NEAR(unpacked.cones[0].width, footprint.cones[0].width, 2.0e-3F);
    EXPECT_NEAR(unpacked.cones[1].angle, footprint.cones[1].angle, 1.0e-3F);
    EXPECT_NEAR(unpacked.cones[1].width, footprint.cones[1].width, 1.0e-3F);
}

TEST(PathIntegratorTests, ReflectsAndRefractsRayCones) {
    auto reflected = flux::RayCone{.angle = 0.1F, .width = 0.2F};
    reflected.reflect(0.5F);
    EXPECT_FLOAT_EQ(reflected.angle, 0.3F);
    EXPECT_FLOAT_EQ(reflected.width, 0.2F);

    auto refracted = flux::RayCone{.angle = 0.1F, .width = 0.2F};
    refracted.refract(0.5F, 2.0F);
    EXPECT_NEAR(refracted.angle, 0.0F, 1.0e-7F);
    EXPECT_FLOAT_EQ(refracted.width, 0.2F);
}

TEST(PathIntegratorTests, ExpandsRayConesFromBsdfValue) {
    auto cone = flux::RayCone{.angle = 0.0F, .width = -0.2F};
    cone.scatter(std::numbers::inv_pi_v<float>);

    EXPECT_FLOAT_EQ(cone.angle, 0.25F);
    EXPECT_FLOAT_EQ(cone.width, 0.2F);
}

TEST(PathIntegratorTests, BroadensConvergingRayCones) {
    auto cone = flux::RayCone{.angle = -0.1F, .width = 0.2F};
    cone.scatter(2.0F * std::numbers::inv_pi_v<float>);

    EXPECT_NEAR(cone.angle, 0.025F, 1.0e-7F);
    EXPECT_FLOAT_EQ(cone.width, 0.2F);
}

TEST(PathIntegratorTests, ProjectsFootprintsAtNormalIncidence) {
    auto const normal = flux::Vec3f{1.0F, -1.0F, 1.0F}.normalize();
    auto const footprint = flux::RayFootprint{
        .cones = {flux::RayCone{.width = 0.2F}, flux::RayCone{}},
    };
    auto dpdx = flux::Vec3f{};
    auto dpdy = flux::Vec3f{};

    footprint.project(normal, normal, dpdx, dpdy);

    EXPECT_NEAR(dpdx.norm(), 0.2F, 1.0e-6F);
    EXPECT_NEAR(dpdy.norm(), 0.2F, 1.0e-6F);
    EXPECT_NEAR(dpdx.dot(normal), 0.0F, 1.0e-6F);
    EXPECT_NEAR(dpdy.dot(normal), 0.0F, 1.0e-6F);
}

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
    auto const context = EmitterHitContext{
        .edf = flux::ConstantEDF::Impl{.radiance = {2.0F, 2.0F, 2.0F}},
    };
    auto const prim = flux::Primitive::Impl{.edfIndex = 0, .primLightIndex = 0};
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
        state, context, prim, surface, {0.0F, 0.0F, 1.0F}
    );

    EXPECT_EQ(state.radiance, (flux::Spectrum{1.0F, 1.0F, 1.0F}));
}
