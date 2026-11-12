#include <gtest/gtest.h>

#include <stdexcept>
#include <utility>

#include "flux/Scene/Context.h"
#include "flux/Scene/ContextIndexMap.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Scene/TXContext.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Texture.h"

namespace {
class TestRenderObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit TestRenderObject(flux::TXContext &tx, kira::Properties const &props)
        : RenderObject(tx), answer(props.use_or<int>("answer", 0)) {}

public:
    int answer;
};

class TrackedRenderObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit TrackedRenderObject(flux::TXContext &tx, kira::Properties const &) : RenderObject(tx) {
        ++liveCount;
    }

public:
    ~TrackedRenderObject() override { --liveCount; }

    static inline int liveCount{0};
};

class OwnerProbe final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit OwnerProbe(flux::TXContext &tx, kira::Properties const &)
        : RenderObject(tx), ownedWhileConstructed(getContext() != nullptr) {}

public:
    bool ownedWhileConstructed;
};

class ThrowingConstructor final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit ThrowingConstructor(flux::TXContext &tx, kira::Properties const &) : RenderObject(tx) {
        (void)tx.create<TrackedRenderObject>(kira::Properties{});
        throw std::runtime_error("constructor failed");
    }
};

} // namespace

TEST(ContextTests, OwnsObjectsCreatedFromProperties) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("answer", 42);

    auto object = context->create<TestRenderObject>(properties);
    auto const id = object->getContextId();

    EXPECT_EQ(object->getContext(), context.get());
    EXPECT_EQ(object->answer, 42);
    EXPECT_EQ(context->getNumContextObjects(), 1);
    EXPECT_EQ(context->get<TestRenderObject>(id), object);
    EXPECT_EQ(std::as_const(*context).get<TestRenderObject>(id), object);

    auto *const rawObject = object.get();
    object.reset();
    EXPECT_EQ(context->get<TestRenderObject>(id).get(), rawObject);
}

TEST(ContextTests, ClearsOwnerWhenContextIsDestroyed) {
    auto context = flux::Context::create();
    auto object = context->create<TestRenderObject>(kira::Properties{});

    context.reset();

    EXPECT_EQ(object->getContext(), nullptr);
}

TEST(ContextTests, GivesAnOwnerOnlyWhenAbsorbing) {
    auto context = flux::Context::create();
    auto probe = context->create<OwnerProbe>(kira::Properties{});

    EXPECT_FALSE(probe->ownedWhileConstructed);
    EXPECT_EQ(probe->getContext(), context.get());
}

TEST(ContextTests, RollsBackConstructionFailures) {
    auto context = flux::Context::create();

    EXPECT_THROW(
        (void)context->create<ThrowingConstructor>(kira::Properties{}), std::runtime_error
    );
    EXPECT_EQ(context->getNumContextObjects(), 0);
    EXPECT_EQ(TrackedRenderObject::liveCount, 0);
}

TEST(ContextTests, RejectsUnknownIds) {
    auto context = flux::Context::create();
    auto object = context->create<TestRenderObject>(kira::Properties{});

    EXPECT_THROW(
        (void)context->get<TestRenderObject>(object->getContextId() + 1), std::out_of_range
    );
}

TEST(ContextIndexMapTests, KeepsAndReusesIndices) {
    flux::ContextIndexMap indices;
    flux::ContextIndexMap::Transaction first;
    first.insert(10);
    first.insert(20);
    indices.merge(std::move(first));

    auto const retainedIndex = indices.getIndex(20);
    auto const releasedIndex = indices.erase(10);
    flux::ContextIndexMap::Transaction second;
    second.insert(30);
    indices.merge(std::move(second));

    EXPECT_EQ(indices.getIndex(20), retainedIndex);
    EXPECT_EQ(indices.getIndex(30), releasedIndex);
    EXPECT_EQ(indices.getIndexLimit(), 2);
}

TEST(ContextTests, TracksBSDFAndEDFIndices) {
    auto context = flux::Context::create();
    kira::Properties bsdfProps;
    bsdfProps.set("type", "diffuse");

    auto firstBSDF = context->create<flux::BSDF>(bsdfProps);
    auto secondBSDF = context->create<flux::BSDF>(bsdfProps);
    auto edf = context->create<flux::EDF>(kira::Properties{});

    EXPECT_NE(
        context->getIndex<flux::BSDF>(firstBSDF->getContextId()),
        context->getIndex<flux::BSDF>(secondBSDF->getContextId())
    );
    EXPECT_NO_THROW((void)context->getIndex<flux::EDF>(edf->getContextId()));
    EXPECT_THROW((void)context->getIndex<flux::EDF>(firstBSDF->getContextId()), std::out_of_range);
}

TEST(ContextTests, OnlyIndexedObjectsEnterTheMapOfTheirKind) {
    auto context = flux::Context::create();
    kira::Properties bsdfProps;
    bsdfProps.set("type", "diffuse");
    (void)context->create<flux::BSDF>(bsdfProps);
    (void)context->create<flux::EDF>(kira::Properties{});
    (void)context->create<TestRenderObject>(kira::Properties{});

    EXPECT_EQ(context->getIndexLimit<flux::BSDF>(), 1);
    EXPECT_EQ(context->getIndexLimit<flux::EDF>(), 1);
    EXPECT_EQ(context->getIndexLimit<flux::ImageTexture>(), 0);
    EXPECT_EQ(context->getObjects<flux::BSDF>().size(), 1);
    EXPECT_EQ(context->getObjects<flux::DiffuseBSDF>().size(), 1);
    EXPECT_EQ(context->getObjects<flux::PrincipledBSDF>().size(), 0);
}
