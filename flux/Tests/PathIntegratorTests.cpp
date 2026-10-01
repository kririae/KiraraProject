#include <gtest/gtest.h>

#include <array>
#include <cmath>
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
        (void)tx.create<flux::PathIntegrator>(kira::Properties{});
        throw std::runtime_error("intentional integrator transaction failure");
    }
};

struct EmitterHitContext {
    struct Table {
        flux::EDF::Impl edf;

        [[nodiscard]] flux::EDF::Impl const &getEDF(std::uint32_t) const noexcept { return edf; }
    } table;

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

TEST(PathIntegratorTests, RefractsRayCones) {
    auto refracted = flux::RayCone{.angle = 0.1F, .width = 0.2F};
    refracted.refract(2.0F);
    EXPECT_FLOAT_EQ(refracted.angle, 0.05F);
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

TEST(PathIntegratorTests, ProjectsOrthogonalFootprintAxes) {
    auto const normal = flux::Vec3f{0.0F, 0.0F, 1.0F};
    for (auto const width : {0.0F, 0.2F}) {
        auto const footprint = flux::RayFootprint{
            .cones = {flux::RayCone{.width = width}, flux::RayCone{}},
        };
        for (auto const cosine : {1.0F, 0.5F, 0.01F, 0.0F}) {
            auto const direction = flux::Vec3f{std::sqrt(1.0F - cosine * cosine), 0.0F, cosine};
            flux::Vec3f dx{}, dy{};
            footprint.project(direction, normal, dx, dy);
            EXPECT_NEAR(dx.norm(), width / std::max(cosine, 1.0F / 16.0F), 1.0e-6F);
            EXPECT_NEAR(dy.norm(), width, 1.0e-6F);
            EXPECT_NEAR(dx.dot(dy), 0.0F, 1.0e-6F);
            EXPECT_NEAR(dx.dot(normal), 0.0F, 1.0e-6F);
            EXPECT_NEAR(dy.dot(normal), 0.0F, 1.0e-6F);
        }
    }
}

TEST(PathIntegratorTests, KeepsFirstSuccessfulIntegratorActive) {
    auto context = flux::Context::create();

    EXPECT_THROW((void)context->getActiveIntegrator(), kira::Anyhow);
    EXPECT_THROW(
        (void)context->create<ThrowingIntegratorOwner>(kira::Properties{}), std::runtime_error
    );
    EXPECT_THROW((void)context->getActiveIntegrator(), kira::Anyhow);

    auto first = context->create<flux::PathIntegrator>(kira::Properties{});
    EXPECT_EQ(context->getActiveIntegrator().get(), first.get());
    EXPECT_FALSE(first->usesShaderReorder());

    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    EXPECT_EQ(context->getActiveIntegrator().get(), first.get());
}

TEST(PathIntegratorTests, EnablesShaderReorderingFromProperties) {
    auto context = flux::Context::create();
    kira::Properties props;
    props.set("shader_reorder", true);

    auto integrator = context->create<flux::PathIntegrator>(props);

    EXPECT_TRUE(integrator->usesShaderReorder());
}

TEST(PathIntegratorTests, WeightsBsdfSampledEmitterHits) {
    auto const context = EmitterHitContext{
        .table = {.edf = flux::ConstantEDF::Impl{.radiance = {2.0F, 2.0F, 2.0F}}},
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
