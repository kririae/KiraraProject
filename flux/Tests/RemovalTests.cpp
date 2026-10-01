#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

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
using Bits = flux::Context::DirtyBits;

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

[[nodiscard]] bool listed(std::span<std::size_t const> ids, std::size_t id) {
    return std::ranges::find(ids, id) != ids.end();
}
} // namespace

TEST(RemovalTests, RemovesARootAndListsItsIndex) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto first = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    auto second = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    auto const index = context->getIndex<flux::Primitive>(first->getContextId());
    context->clearDirty();

    context->remove(first->getContextId());

    EXPECT_EQ(context->getDirtyBits(), Bits::Removed);
    EXPECT_EQ(first->getContext(), nullptr);
    EXPECT_EQ(context->getObjects<flux::Primitive>().size(), 1U);
    EXPECT_THROW(
        (void)context->getIndex<flux::Primitive>(first->getContextId()), std::out_of_range
    );
    EXPECT_THROW((void)context->get<flux::Primitive>(first->getContextId()), std::out_of_range);
    auto const removals = context->getRemovals();
    ASSERT_EQ(removals.size(), 1U);
    EXPECT_EQ(removals[0].kind, flux::IndexedKind::Primitive);
    EXPECT_EQ(removals[0].index, index);
    EXPECT_EQ(second->getContext(), context.get());

    // The host's reference keeps the object alive, and its setters record nothing.
    first->setVisible(false);
    EXPECT_EQ(flux::getDirtyBits(*first), flux::Primitive::DirtyBits::None);
    EXPECT_TRUE(context->getChangedIds().empty());
}

TEST(RemovalTests, RemovingANonIndexedRootAddsNoRemoval) {
    auto context = flux::Context::create();
    auto light = context->create<flux::PointLight>(kira::Properties{});
    context->clearDirty();

    context->remove(light->getContextId());

    EXPECT_EQ(context->getDirtyBits(), Bits::Removed);
    EXPECT_TRUE(context->getRemovals().empty());
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
    EXPECT_EQ(context->getDirtyBits(), Bits::None);
    EXPECT_TRUE(context->getRemovals().empty());
    EXPECT_EQ(context->getChangedIds().size(), 1U);
    EXPECT_EQ(mesh->getContext(), context.get());
}

TEST(RemovalTests, UnlistsAChangedObject) {
    auto context = flux::Context::create();
    auto first = context->create<flux::PointLight>(kira::Properties{});
    auto second = context->create<flux::PointLight>(kira::Properties{});
    context->clearDirty();
    first->setIntensity({2.0F, 2.0F, 2.0F});
    second->setIntensity({2.0F, 2.0F, 2.0F});

    context->remove(first->getContextId());

    ASSERT_EQ(context->getChangedIds().size(), 1U);
    EXPECT_EQ(context->getChangedIds()[0], second->getContextId());
    EXPECT_EQ(flux::getDirtyBits(*first), flux::PointLight::DirtyBits::None);
}

TEST(RemovalTests, UnlistsAnAddedObject) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto primitive = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));

    context->remove(primitive->getContextId());

    EXPECT_FALSE(listed(context->getAddedIds(), primitive->getContextId()));
    EXPECT_TRUE(listed(context->getAddedIds(), mesh->getContextId()));
}

TEST(RemovalTests, RemovingTheActiveEnvMapEmptiesTheSlot) {
    auto context = flux::Context::create();
    auto env = context->create<flux::EnvMapLight>(kira::Properties{});
    auto integrator = context->create<flux::PathIntegrator>(kira::Properties{});
    context->clearDirty();

    context->remove(env->getContextId());

    EXPECT_FALSE(context->getActiveEnvMap());
    EXPECT_EQ(context->getDirtyBits(), Bits::Removed | Bits::ActiveEnvMap);
    EXPECT_EQ(context->getActiveIntegrator(), integrator);
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
    EXPECT_EQ(context->getDirtyBits(), Bits::None);
    EXPECT_NO_THROW((void)context->get<flux::Geometry>(meshId));

    context->remove(primitive->getContextId());
    primitive.reset();
    context->collectGarbage();

    EXPECT_EQ(context->getNumContextObjects(), 0U);
    EXPECT_THROW((void)context->get<flux::Geometry>(meshId), std::out_of_range);
    EXPECT_THROW((void)context->get<flux::BSDF>(bsdfId), std::out_of_range);
    EXPECT_THROW((void)context->get<flux::EDF>(edfId), std::out_of_range);
    EXPECT_EQ(context->getRemovals().size(), 4U);
    EXPECT_EQ(context->getDirtyBits(), Bits::Removed);
}

TEST(RemovalTests, KeepsDependentsThatAreStillReferenced) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto first = context->create<flux::Primitive>(primitiveProperties(*mesh, bsdf.get(), nullptr));
    auto second = context->create<flux::Primitive>(primitiveProperties(*mesh, bsdf.get(), nullptr));
    auto const meshId = mesh->getContextId();
    auto const bsdfId = bsdf->getContextId();
    mesh.reset();
    bsdf.reset();

    // Another primitive references both dependents.
    context->remove(first->getContextId());
    first.reset();
    context->collectGarbage();
    EXPECT_NO_THROW((void)context->get<flux::Geometry>(meshId));
    EXPECT_NO_THROW((void)context->get<flux::BSDF>(bsdfId));

    // A host reference keeps the BSDF, until the host releases it.
    auto hostBSDF = context->get<flux::BSDF>(bsdfId);
    context->remove(second->getContextId());
    second.reset();
    context->collectGarbage();
    EXPECT_THROW((void)context->get<flux::Geometry>(meshId), std::out_of_range);
    EXPECT_NO_THROW((void)context->get<flux::BSDF>(bsdfId));

    hostBSDF.reset();
    context->collectGarbage();
    EXPECT_THROW((void)context->get<flux::BSDF>(bsdfId), std::out_of_range);
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
    auto const objects = context->getNumContextObjects();
    ASSERT_GT(objects, 4U);

    // The primitive holds the BSDF, which holds its textures.
    context->remove(primitive->getContextId());
    primitive.reset();
    context->remove(env->getContextId());
    env.reset();
    context->collectGarbage();

    EXPECT_EQ(context->getNumContextObjects(), 0U);
    EXPECT_THROW((void)context->get<flux::Texture>(textureId), std::out_of_range);
}

TEST(RemovalTests, ReusesAReleasedIndexOnlyAfterClearDirty) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto first = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    auto const index = context->getIndex<flux::Primitive>(first->getContextId());
    context->clearDirty();

    context->remove(first->getContextId());
    auto second = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    EXPECT_NE(context->getIndex<flux::Primitive>(second->getContextId()), index);

    context->clearDirty();
    EXPECT_TRUE(context->getRemovals().empty());
    auto third = context->create<flux::Primitive>(primitiveProperties(*mesh, nullptr, nullptr));
    EXPECT_EQ(context->getIndex<flux::Primitive>(third->getContextId()), index);
}
