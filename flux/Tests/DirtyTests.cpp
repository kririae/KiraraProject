#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Texture.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

namespace {
[[nodiscard]] kira::Properties triangleProperties() {
    kira::Properties properties;
    properties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    return properties;
}

[[nodiscard]] flux::Ref<flux::Primitive>
createPrimitive(flux::Context &context, flux::Ref<flux::TriangleMesh> const &mesh) {
    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    return context.create<flux::Primitive>(properties);
}

constexpr std::array<float, 12> moved{
    1.0F, 0.0F, 0.0F, 2.0F, 0.0F, 1.0F, 0.0F, 3.0F, 0.0F, 0.0F, 1.0F, 4.0F,
};
} // namespace

TEST(DirtyTests, NewObjectsAreClean) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto primitive = createPrimitive(*context, mesh);
    auto point = context->create<flux::PointLight>(kira::Properties{});
    auto env = context->create<flux::EnvMapLight>(kira::Properties{});
    auto edf = context->create<flux::ConstantEDF>(kira::Properties{});

    EXPECT_EQ(flux::getDirtyBits(*primitive), flux::Primitive::DirtyBits::None);
    EXPECT_EQ(flux::getDirtyBits(*point), flux::PointLight::DirtyBits::None);
    EXPECT_EQ(flux::getDirtyBits(*env), flux::EnvMapLight::DirtyBits::None);
    EXPECT_EQ(flux::getDirtyBits(*edf), flux::ConstantEDF::DirtyBits::None);
    EXPECT_TRUE(context->getChangedIds().empty());
    EXPECT_EQ(context->getEpoch(), 0U);
}

TEST(DirtyTests, PrimitiveSettersRecordTheirBits) {
    using Bits = flux::Primitive::DirtyBits;
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto other = context->create<flux::TriangleMesh>(triangleProperties());
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto edf = context->create<flux::ConstantEDF>(kira::Properties{});
    auto primitive = createPrimitive(*context, mesh);

    primitive->setGeometry(other);
    EXPECT_EQ(flux::getDirtyBits(*primitive), Bits::Geometry);
    context->clearDirty();

    primitive->setBSDF(bsdf);
    EXPECT_EQ(flux::getDirtyBits(*primitive), Bits::BSDF);
    context->clearDirty();

    primitive->setEDF(edf);
    EXPECT_EQ(flux::getDirtyBits(*primitive), Bits::EDF);
    context->clearDirty();

    primitive->setTransform(moved);
    EXPECT_EQ(flux::getDirtyBits(*primitive), Bits::Transform);
    context->clearDirty();

    primitive->setVisible(false);
    EXPECT_EQ(flux::getDirtyBits(*primitive), Bits::Visibility);
}

TEST(DirtyTests, PointLightSettersRecordTheirBits) {
    using Bits = flux::PointLight::DirtyBits;
    auto context = flux::Context::create();
    auto light = context->create<flux::PointLight>(kira::Properties{});

    light->setPosition({1.0F, 2.0F, 3.0F});
    EXPECT_EQ(flux::getDirtyBits(*light), Bits::Position);
    context->clearDirty();

    light->setIntensity({2.0F, 2.0F, 2.0F});
    EXPECT_EQ(flux::getDirtyBits(*light), Bits::Intensity);
}

TEST(DirtyTests, EnvMapLightSettersRecordTheirBits) {
    using Bits = flux::EnvMapLight::DirtyBits;
    auto context = flux::Context::create();
    auto light = context->create<flux::EnvMapLight>(kira::Properties{});
    kira::Properties properties;
    properties.set("value", 0.5F);
    auto other = context->create<flux::ConstantTexture>(properties);

    light->setTexture(other);
    EXPECT_EQ(flux::getDirtyBits(*light), Bits::Texture);
    context->clearDirty();

    light->setScale({2.0F, 2.0F, 2.0F});
    EXPECT_EQ(flux::getDirtyBits(*light), Bits::Scale);
    context->clearDirty();

    light->setRotation({10.0F, 0.0F, 0.0F});
    EXPECT_EQ(flux::getDirtyBits(*light), Bits::Rotation);
}

TEST(DirtyTests, ConstantEDFSetterRecordsItsBit) {
    auto context = flux::Context::create();
    auto edf = context->create<flux::ConstantEDF>(kira::Properties{});

    edf->setRadiance({3.0F, 3.0F, 3.0F});

    EXPECT_EQ(flux::getDirtyBits(*edf), flux::ConstantEDF::DirtyBits::Radiance);
}

TEST(DirtyTests, ListsAnObjectOnceAfterSeveralSetters) {
    using Bits = flux::Primitive::DirtyBits;
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto first = createPrimitive(*context, mesh);
    auto second = createPrimitive(*context, mesh);

    second->setVisible(false);
    first->setTransform(moved);
    first->setVisible(false);
    second->setTransform(moved);

    EXPECT_EQ(flux::getDirtyBits(*first), Bits::Transform | Bits::Visibility);
    EXPECT_EQ(flux::getDirtyBits(*second), Bits::Transform | Bits::Visibility);
    auto const ids = context->getChangedIds();
    ASSERT_EQ(ids.size(), 2U);
    EXPECT_EQ(ids[0], second->getContextId());
    EXPECT_EQ(ids[1], first->getContextId());
}

TEST(DirtyTests, AssigningTheCurrentValueRecordsNothing) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto primitive = createPrimitive(*context, mesh);
    auto point = context->create<flux::PointLight>(kira::Properties{});
    auto env = context->create<flux::EnvMapLight>(kira::Properties{});
    auto edf = context->create<flux::ConstantEDF>(kira::Properties{});

    primitive->setGeometry(primitive->getGeometry());
    primitive->setBSDF(nullptr);
    primitive->setEDF(nullptr);
    primitive->setTransform(primitive->getTransform());
    primitive->setVisible(true);
    point->setPosition(point->getPosition());
    point->setIntensity(point->getIntensity());
    env->setTexture(env->getTexture());
    env->setScale(env->getScale());
    env->setRotation(env->getRotation());
    edf->setRadiance(edf->getRadiance());

    EXPECT_TRUE(context->getChangedIds().empty());
    EXPECT_EQ(flux::getDirtyBits(*primitive), flux::Primitive::DirtyBits::None);
    EXPECT_EQ(flux::getDirtyBits(*env), flux::EnvMapLight::DirtyBits::None);
}

TEST(DirtyTests, ComparesTheClampedEnvironmentMapScale) {
    auto context = flux::Context::create();
    auto env = context->create<flux::EnvMapLight>(kira::Properties{});
    env->setScale({-1.0F, 1.0F, 1.0F});
    context->clearDirty();

    env->setScale({-2.0F, 1.0F, 1.0F});
    EXPECT_TRUE(context->getChangedIds().empty());

    env->setScale({-2.0F, 1.0F, 1.0F});
    env->setScale({0.0F, 1.0F, 2.0F});
    EXPECT_EQ(flux::getDirtyBits(*env), flux::EnvMapLight::DirtyBits::Scale);
}

TEST(DirtyTests, ClearDirtyResetsBitsAndListAndAdvancesTheEpoch) {
    auto context = flux::Context::create();
    auto edf = context->create<flux::ConstantEDF>(kira::Properties{});
    edf->setRadiance({3.0F, 3.0F, 3.0F});
    ASSERT_EQ(context->getChangedIds().size(), 1U);

    context->clearDirty();

    EXPECT_EQ(flux::getDirtyBits(*edf), flux::ConstantEDF::DirtyBits::None);
    EXPECT_TRUE(context->getChangedIds().empty());
    EXPECT_EQ(context->getEpoch(), 1U);

    edf->setRadiance({4.0F, 4.0F, 4.0F});
    EXPECT_EQ(context->getChangedIds().size(), 1U);
    context->clearDirty();
    EXPECT_EQ(context->getEpoch(), 2U);
}

TEST(DirtyTests, ObjectOutlivingItsContextRecordsNothing) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto primitive = createPrimitive(*context, mesh);
    context.reset();

    primitive->setVisible(false);
    primitive->setTransform(moved);

    EXPECT_EQ(flux::getDirtyBits(*primitive), flux::Primitive::DirtyBits::None);
}

TEST(DirtyTests, FailedValidationRecordsNothing) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto primitive = createPrimitive(*context, mesh);
    auto point = context->create<flux::PointLight>(kira::Properties{});

    EXPECT_THROW(primitive->setGeometry(nullptr), kira::Anyhow);
    EXPECT_THROW(point->setIntensity({-1.0F, 0.0F, 0.0F}), kira::Anyhow);

    EXPECT_TRUE(context->getChangedIds().empty());
    EXPECT_EQ(flux::getDirtyBits(*primitive), flux::Primitive::DirtyBits::None);
    EXPECT_EQ(flux::getDirtyBits(*point), flux::PointLight::DirtyBits::None);
}

TEST(DirtyTests, CreateListsNewObjectsAsAdded) {
    using Bits = flux::Context::DirtyBits;
    auto context = flux::Context::create();
    EXPECT_EQ(context->getDirtyBits(), Bits::None);

    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto primitive = createPrimitive(*context, mesh);

    EXPECT_EQ(context->getDirtyBits(), Bits::Added);
    auto const added = context->getAddedIds();
    ASSERT_EQ(added.size(), 2U);
    EXPECT_EQ(added[0], mesh->getContextId());
    EXPECT_EQ(added[1], primitive->getContextId());
    EXPECT_TRUE(context->getChangedIds().empty());
    EXPECT_EQ(flux::getDirtyBits(*primitive), flux::Primitive::DirtyBits::None);
}

TEST(DirtyTests, NestedCreationListsChildrenInAscendingOrder) {
    auto context = flux::Context::create();
    auto properties = triangleProperties();
    properties.set("type", "trimesh");
    kira::Properties bsdfProperties;
    bsdfProperties.set("type", "diffuse");
    properties.set("bsdf", bsdfProperties);

    auto primitive = context->create<flux::Primitive>(properties);

    auto const added = context->getAddedIds();
    EXPECT_EQ(added.size(), context->getNumContextObjects());
    EXPECT_TRUE(std::ranges::is_sorted(added));
    EXPECT_NE(std::ranges::find(added, primitive->getGeometry()->getContextId()), added.end());
    EXPECT_NE(std::ranges::find(added, primitive->getBSDF()->getContextId()), added.end());
    EXPECT_NE(std::ranges::find(added, primitive->getContextId()), added.end());
    EXPECT_TRUE(context->getChangedIds().empty());
}

TEST(DirtyTests, FirstActiveObjectsRecordTheirBits) {
    using Bits = flux::Context::DirtyBits;
    auto context = flux::Context::create();

    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    EXPECT_EQ(context->getDirtyBits(), Bits::Added | Bits::ActiveIntegrator);
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
    (void)context->create<flux::EnvMapLight>(kira::Properties{});

    EXPECT_EQ(
        context->getDirtyBits(),
        Bits::Added | Bits::ActiveIntegrator | Bits::ActiveSampler | Bits::ActiveEnvMap
    );
}

TEST(DirtyTests, SecondActiveObjectsRecordNothing) {
    using Bits = flux::Context::DirtyBits;
    auto context = flux::Context::create();
    (void)context->create<flux::EnvMapLight>(kira::Properties{});
    context->clearDirty();

    (void)context->create<flux::EnvMapLight>(kira::Properties{});

    EXPECT_EQ(context->getDirtyBits(), Bits::Added);
}

TEST(DirtyTests, ActiveSettersRecordOnlyChanges) {
    using Bits = flux::Context::DirtyBits;
    auto context = flux::Context::create();
    auto integrator = context->create<flux::PathIntegrator>(kira::Properties{});
    auto otherIntegrator = context->create<flux::PathIntegrator>(kira::Properties{});
    auto sampler = context->create<flux::IndependentSampler>(kira::Properties{});
    auto otherSampler = context->create<flux::IndependentSampler>(kira::Properties{});
    auto env = context->create<flux::EnvMapLight>(kira::Properties{});
    auto otherEnv = context->create<flux::EnvMapLight>(kira::Properties{});
    context->clearDirty();

    context->setActiveIntegrator(integrator);
    context->setActiveSampler(sampler);
    context->setActiveEnvMap(env);
    EXPECT_EQ(context->getDirtyBits(), Bits::None);

    context->setActiveIntegrator(otherIntegrator);
    EXPECT_EQ(context->getActiveIntegrator(), otherIntegrator);
    EXPECT_EQ(context->getDirtyBits(), Bits::ActiveIntegrator);
    context->setActiveSampler(otherSampler);
    EXPECT_EQ(context->getActiveSampler(), otherSampler);
    context->setActiveEnvMap(otherEnv);
    EXPECT_EQ(context->getActiveEnvMap(), otherEnv);
    EXPECT_EQ(
        context->getDirtyBits(), Bits::ActiveIntegrator | Bits::ActiveSampler | Bits::ActiveEnvMap
    );
    EXPECT_TRUE(context->getChangedIds().empty());
}

TEST(DirtyTests, NullEnvironmentMapClearsTheActiveOne) {
    auto context = flux::Context::create();
    (void)context->create<flux::EnvMapLight>(kira::Properties{});
    context->clearDirty();

    context->setActiveEnvMap(nullptr);

    EXPECT_FALSE(context->getActiveEnvMap());
    EXPECT_EQ(context->getDirtyBits(), flux::Context::DirtyBits::ActiveEnvMap);

    context->clearDirty();
    context->setActiveEnvMap(nullptr);
    EXPECT_EQ(context->getDirtyBits(), flux::Context::DirtyBits::None);
}

TEST(DirtyTests, ActiveSettersRejectForeignAndNullObjects) {
    auto context = flux::Context::create();
    auto other = flux::Context::create();
    auto integrator = context->create<flux::PathIntegrator>(kira::Properties{});
    auto sampler = context->create<flux::IndependentSampler>(kira::Properties{});
    auto env = context->create<flux::EnvMapLight>(kira::Properties{});
    auto foreignIntegrator = other->create<flux::PathIntegrator>(kira::Properties{});
    auto foreignSampler = other->create<flux::IndependentSampler>(kira::Properties{});
    auto foreignEnv = other->create<flux::EnvMapLight>(kira::Properties{});
    context->clearDirty();

    EXPECT_THROW(context->setActiveIntegrator(foreignIntegrator), kira::Anyhow);
    EXPECT_THROW(context->setActiveIntegrator(nullptr), kira::Anyhow);
    EXPECT_THROW(context->setActiveSampler(foreignSampler), kira::Anyhow);
    EXPECT_THROW(context->setActiveSampler(nullptr), kira::Anyhow);
    EXPECT_THROW(context->setActiveEnvMap(foreignEnv), kira::Anyhow);

    EXPECT_EQ(context->getDirtyBits(), flux::Context::DirtyBits::None);
    EXPECT_EQ(context->getActiveIntegrator(), integrator);
    EXPECT_EQ(context->getActiveSampler(), sampler);
    EXPECT_EQ(context->getActiveEnvMap(), env);
}

TEST(DirtyTests, ClearDirtyEmptiesTheAddedListAndContextBits) {
    auto context = flux::Context::create();
    (void)context->create<flux::EnvMapLight>(kira::Properties{});
    ASSERT_FALSE(context->getAddedIds().empty());

    context->clearDirty();

    EXPECT_TRUE(context->getAddedIds().empty());
    EXPECT_EQ(context->getDirtyBits(), flux::Context::DirtyBits::None);
}
