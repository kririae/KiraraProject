#include <gtest/gtest.h>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/EDF.h"

namespace {
struct EmitterHitContext {
    struct Table {
        flux::EDF::Impl edf;

        [[nodiscard]] flux::EDF::Impl const &getEDF(std::uint32_t) const noexcept { return edf; }
    } table;

    [[nodiscard]] float pdfDirectLight(
        flux::LightSamplingContext const &, flux::SurfaceInteraction const &
    ) const noexcept {
        return 0.5F;
    }
};
} // namespace

TEST(PathIntegratorTests, ActivatesOnlyTheIntegratorTheHostSets) {
    auto context = flux::Context::create();

    EXPECT_FALSE(context->getActiveIntegrator());
    auto first = context->create<flux::PathIntegrator>(kira::Properties{});
    EXPECT_FALSE(context->getActiveIntegrator());
    EXPECT_FALSE(first->usesShaderReorder());

    context->setActiveIntegrator(first);
    EXPECT_EQ(context->getActiveIntegrator().get(), first.get());
}

TEST(PathIntegratorTests, ReadsShaderReorderingAndChunkSizeFromProperties) {
    auto context = flux::Context::create();
    kira::Properties props;
    props.set("shader_reorder", true);
    props.set("chunk_size", std::uint32_t{32});

    auto integrator = context->create<flux::PathIntegrator>(props);

    EXPECT_TRUE(integrator->usesShaderReorder());
    EXPECT_EQ(integrator->getChunkSize(), 32U);

    props.set("chunk_size", std::uint32_t{0});
    EXPECT_EQ(context->create<flux::PathIntegrator>(props)->getChunkSize(), 1U);
}

TEST(PathIntegratorTests, BoundsRussianRouletteProbabilityToUnitRange) {
    auto context = flux::Context::create();
    kira::Properties props;

    props.set("rr_prob", -0.5F);
    EXPECT_EQ(context->create<flux::PathIntegrator>(props)->getImpl().rrProb, 0.0F);

    props.set("rr_prob", 1.5F);
    EXPECT_EQ(context->create<flux::PathIntegrator>(props)->getImpl().rrProb, 1.0F);
}

TEST(PathIntegratorTests, WeightsBsdfSampledEmitterHits) {
    auto const context = EmitterHitContext{
        .table = {.edf = flux::ConstantEDF::Impl{.radiance = {2.0F, 2.0F, 2.0F}}},
    };
    auto const prim = flux::Primitive::Impl{.edfIndex = 0};
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
