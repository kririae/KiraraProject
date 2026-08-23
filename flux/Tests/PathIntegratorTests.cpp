#include <gtest/gtest.h>

#include <stdexcept>

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
    auto const prim = flux::Primitive::Impl{.edfIndex = 0, .lightIndex = 0};
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
