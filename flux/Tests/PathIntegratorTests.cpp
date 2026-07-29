#include <gtest/gtest.h>

#include <array>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Scene/TXContext.h"
#include "flux/Shading/DiffuseBSDFImpl.h"
#include "kira/Anyhow.h"

namespace {
struct TestScene {
    flux::LightSampler lightSampler;

    [[nodiscard]] flux::LightSampler const &getLightSampler() const noexcept {
        return lightSampler;
    }
};

class ThrowingIntegratorOwner final : public flux::RenderObject {
    friend class flux::TXContext;

    ThrowingIntegratorOwner(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {
        (void)tx.create<flux::PathIntegrator>();
        throw std::runtime_error("intentional integrator transaction failure");
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

    (void)context->create<flux::PathIntegrator>();
    EXPECT_EQ(context->getActiveIntegrator().get(), first.get());
}

TEST(PathIntegratorTests, TerminatesCompletedPathsOnHost) {
    auto state = flux::PathState{};

    flux::PathIntegrator::Impl{}.onMiss(state);

    EXPECT_FALSE(state.active);
}

TEST(PathIntegratorTests, DefersDirectLightUntilVisibilityIsKnown) {
    auto context = flux::Context::create();
    auto sampler = context->create<flux::IndependentSampler>();

    auto state = flux::PathState{};
    state.sampler = flux::Sampler::Impl{
        .type = flux::SamplerType::Independent,
        .storage = {.independent = sampler->getImpl({1, 1})},
    };
    state.sampler.startPixelSample({0, 0}, 0, {1, 1});

    auto const surface = flux::SurfaceInteraction{
        .position = {0.0F, 0.0F, 0.0F},
        .geometricNormal = {0.0F, 0.0F, 1.0F},
        .shadingNormal = {0.0F, 0.0F, 1.0F},
    };
    auto const pointLights = std::array{
        flux::PointLight::Impl{
            .position = {0.0F, 0.0F, 2.0F},
            .intensity = {4.0F, 4.0F, 4.0F},
        },
    };
    auto const records =
        std::array{flux::LightRecord{.type = flux::LightType::Point, .typedIndex = 0}};
    auto const lightSampler = flux::LightSampler{
        .lights = {
            .records = records.data(),
            .pointLights = pointLights.data(),
            .numLights = 1,
        },
    };
    auto bsdf = flux::DiffuseBSDF::Impl{};
    bsdf.reflectance = flux::Spectrum{0.5F, 0.5F, 0.5F};
    auto const integrator = flux::PathIntegrator::Impl{};
    auto const scene = TestScene{.lightSampler = lightSampler};

    integrator.onSurfaceHit(state, scene, bsdf, surface, flux::Vec3f{0.0F, 0.0F, 1.0F});

    EXPECT_FALSE(state.active);
    ASSERT_TRUE(state.hasPendingShadowQuery);
    auto occluded = state;
    integrator.resolvePendingShadowQuery(occluded, false);
    EXPECT_EQ(occluded.radiance, flux::Spectrum{});

    integrator.resolvePendingShadowQuery(state, true);
    auto const expected = 0.5F * std::numbers::inv_pi_v<float>;
    EXPECT_NEAR(state.radiance.x(), expected, 1.0e-6F);
    EXPECT_NEAR(state.radiance.y(), expected, 1.0e-6F);
    EXPECT_NEAR(state.radiance.z(), expected, 1.0e-6F);
    EXPECT_FALSE(state.hasPendingShadowQuery);
}
