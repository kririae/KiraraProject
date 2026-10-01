#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <unordered_set>

#include "TestUtils.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Texture.h"
#include "kira/Anyhow.h"

namespace {
using ContextBits = flux::Context::DirtyBits;

[[nodiscard]] flux::TriangleMesh::Data triangleData() {
    return {
        .vertices = flux::test::sharedBuffer(
            flux::Vec3f{0.0F, 0.0F, 0.0F}, flux::Vec3f{1.0F, 0.0F, 0.0F},
            flux::Vec3f{0.0F, 1.0F, 0.0F}
        ),
        .triangles = flux::test::sharedBuffer(flux::Vec3u{0, 1, 2}),
    };
}

[[nodiscard]] kira::Properties
primitiveProperties(flux::Geometry const &mesh, flux::BSDF const *bsdf, flux::EDF const *edf) {
    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh.getContextId()));
    if (bsdf)
        properties.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    if (edf)
        properties.set("edf_ctx_id", static_cast<std::int64_t>(edf->getContextId()));
    return properties;
}

} // namespace

TEST(RemovalTests, RemovesARootAtOnce) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto first = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    auto second = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    auto const index = context->getIndex<flux::Primitive>(first->getContextId());
    context->clearDirty();
    first->setVisible(false);

    context->remove(first->getContextId());

    EXPECT_EQ(first->getContext(), nullptr);
    EXPECT_EQ(context->getObjects<flux::Primitive>().size(), 1U);
    EXPECT_THROW((void)context->get<flux::Primitive>(first->getContextId()), std::out_of_range);
    EXPECT_EQ(context->getRemovedIds(), (std::unordered_set<std::size_t>{first->getContextId()}));
    EXPECT_TRUE(context->getChangedIds().empty());
    EXPECT_EQ(second->getContext(), context.get());

    // The index stays reserved until the clear.
    EXPECT_EQ(context->findIndex<flux::Primitive>(first->getContextId()), index);
    context->clearDirty();
    EXPECT_FALSE(context->findIndex<flux::Primitive>(first->getContextId()));
    EXPECT_TRUE(context->getRemovedIds().empty());

    // The host's reference keeps the object alive, and its setters record nothing.
    first->setVisible(true);
    EXPECT_EQ(flux::getDirtyBits(*first), flux::Primitive::DirtyBits::None);
    EXPECT_TRUE(context->getChangedIds().empty());
}

TEST(RemovalTests, RemovedIdsLeaveTheAddedAndChangedSets) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto added = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    auto changed = context->create<flux::PointLight>(kira::Properties{});
    context->clearDirty();
    auto fresh = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    changed->setIntensity({2.0F, 2.0F, 2.0F});
    fresh->setVisible(false);

    context->remove(fresh->getContextId());
    context->remove(changed->getContextId());

    EXPECT_TRUE(context->getAddedIds().empty());
    EXPECT_TRUE(context->getChangedIds().empty());
    EXPECT_EQ(context->getRemovedIds().size(), 2U);
    EXPECT_EQ(flux::getDirtyBits(*changed), flux::PointLight::DirtyBits::None);
    EXPECT_EQ(added->getContext(), context.get());
}

TEST(RemovalTests, RejectsDependentsAndUnknownIds) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto primitive = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    context->clearDirty();
    primitive->setVisible(false);
    auto const count = context->getNumContextObjects();

    EXPECT_THROW(context->remove(mesh->getContextId()), kira::Anyhow);
    EXPECT_THROW(context->remove(1000), std::out_of_range);

    EXPECT_EQ(context->getNumContextObjects(), count);
    EXPECT_TRUE(context->getRemovedIds().empty());
    EXPECT_EQ(context->getChangedIds().size(), 1U);
    EXPECT_EQ(mesh->getContext(), context.get());
}

TEST(RemovalTests, RemovingAnActiveObjectEmptiesItsSlot) {
    auto context = flux::Context::create();
    auto env = context->create<flux::EnvMapLight>(kira::Properties{});
    auto sampler = context->create<flux::IndependentSampler>(kira::Properties{});
    auto integrator = context->create<flux::PathIntegrator>(kira::Properties{});
    auto other = context->create<flux::EnvMapLight>(kira::Properties{});
    context->setActiveEnvMap(env);
    context->setActiveSampler(sampler);
    context->setActiveIntegrator(integrator);
    context->clearDirty();

    // Removing an object that is not active leaves the slots alone.
    context->remove(other->getContextId());
    EXPECT_EQ(context->getDirtyBits(), ContextBits::None);

    context->remove(env->getContextId());
    EXPECT_FALSE(context->getActiveEnvMap());
    EXPECT_EQ(context->getDirtyBits(), ContextBits::ActiveEnvMap);
    context->remove(sampler->getContextId());
    context->remove(integrator->getContextId());

    EXPECT_FALSE(context->getActiveSampler());
    EXPECT_FALSE(context->getActiveIntegrator());
    EXPECT_EQ(
        context->getDirtyBits(),
        ContextBits::ActiveEnvMap | ContextBits::ActiveSampler | ContextBits::ActiveIntegrator
    );
    EXPECT_EQ(context->getRemovedIds().size(), 4U);
}

TEST(RemovalTests, CollectsDependentsThatNothingReferences) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto edf = context->create<flux::ConstantEDF>(kira::Properties{});
    auto primitive =
        context->create<flux::Primitive>(primitiveProperties(*mesh, bsdf.get(), edf.get()));
    auto const meshId = mesh->getContextId();
    auto const bsdfId = bsdf->getContextId();
    auto const edfId = edf->getContextId();
    mesh.reset();
    bsdf.reset();
    edf.reset();
    context->clearDirty();

    context->collectGarbage();
    EXPECT_TRUE(context->getRemovedIds().empty());
    EXPECT_NO_THROW((void)context->get<flux::Geometry>(meshId));

    context->remove(primitive->getContextId());
    primitive.reset();
    context->collectGarbage();

    EXPECT_EQ(context->getNumContextObjects(), 0U);
    EXPECT_THROW((void)context->get<flux::Geometry>(meshId), std::out_of_range);
    EXPECT_THROW((void)context->get<flux::BSDF>(bsdfId), std::out_of_range);
    EXPECT_THROW((void)context->get<flux::EDF>(edfId), std::out_of_range);
    EXPECT_EQ(context->getRemovedIds().size(), 5U);
    EXPECT_TRUE(context->findIndex<flux::BSDF>(bsdfId));
}

TEST(RemovalTests, CollectsInlineChildrenWithTheirParent) {
    auto context = flux::Context::create();
    auto properties =
        primitiveProperties(*context->create<flux::TriangleMesh>(triangleData()), nullptr, nullptr);
    kira::Properties bsdfProperties;
    bsdfProperties.set("type", "diffuse");
    properties.set("bsdf", bsdfProperties);
    auto primitive = context->create<flux::Primitive>(properties);

    context->remove(primitive->getContextId());
    primitive.reset();
    context->collectGarbage();

    EXPECT_EQ(context->getNumContextObjects(), 0U);
}

TEST(RemovalTests, KeepsDependentsThatAreStillReferenced) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto replacement = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto first = context->create<flux::Primitive>(primitiveProperties(*mesh, bsdf.get(), nullptr));
    auto second = context->create<flux::Primitive>(primitiveProperties(*mesh, bsdf.get(), nullptr));
    auto const meshId = mesh->getContextId();
    auto const bsdfId = bsdf->getContextId();
    mesh.reset();
    auto hostBSDF = bsdf;
    bsdf.reset();
    replacement.reset();

    // Another primitive references both dependents.
    context->remove(first->getContextId());
    first.reset();
    context->collectGarbage();
    EXPECT_NO_THROW((void)context->get<flux::Geometry>(meshId));
    EXPECT_NO_THROW((void)context->get<flux::BSDF>(bsdfId));

    // Rebinding drops the primitive's reference, but the host still holds one.
    second->setBSDF(nullptr);
    context->collectGarbage();
    EXPECT_NO_THROW((void)context->get<flux::BSDF>(bsdfId));

    // The BSDF goes once the host drops its reference.
    hostBSDF.reset();
    context->collectGarbage();
    EXPECT_THROW((void)context->get<flux::BSDF>(bsdfId), std::out_of_range);
    EXPECT_NO_THROW((void)context->get<flux::Geometry>(meshId));
}

TEST(RemovalTests, CollectsAChainToItsFixpoint) {
    auto context = flux::Context::create();
    auto env = context->create<flux::EnvMapLight>(kira::Properties{});
    auto const textureId = env->getTexture()->getContextId();
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto primitive =
        context->create<flux::Primitive>(primitiveProperties(*mesh, bsdf.get(), nullptr));
    mesh.reset();
    bsdf.reset();
    ASSERT_GT(context->getNumContextObjects(), 4U);

    // The primitive holds the BSDF, which holds its textures.
    context->remove(primitive->getContextId());
    primitive.reset();
    context->remove(env->getContextId());
    env.reset();
    context->collectGarbage();

    EXPECT_EQ(context->getNumContextObjects(), 0U);
    EXPECT_THROW((void)context->get<flux::Texture>(textureId), std::out_of_range);
}

TEST(RemovalTests, ReusesARemovedIndexOnlyAfterClearDirty) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto first = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    auto const index = context->getIndex<flux::Primitive>(first->getContextId());
    context->clearDirty();

    context->remove(first->getContextId());
    auto second = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    EXPECT_NE(context->getIndex<flux::Primitive>(second->getContextId()), index);

    context->clearDirty();
    auto third = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    EXPECT_EQ(context->getIndex<flux::Primitive>(third->getContextId()), index);
}

TEST(RemovalTests, GetObjectsSkipsRemovedObjectsBeforeTheClear) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto first = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    auto second = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));

    context->remove(first->getContextId());

    auto const primitives = context->getObjects<flux::Primitive>();
    ASSERT_EQ(primitives.size(), 1U);
    EXPECT_EQ(primitives[0], second);
}
