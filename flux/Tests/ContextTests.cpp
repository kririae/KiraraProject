#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "flux/Scene/Context.h"
#include "flux/Scene/ContextIndexMap.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Primitive.h"
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

class OtherRenderObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit OtherRenderObject(flux::TXContext &tx, kira::Properties const &) : RenderObject(tx) {}
};

class NestedRenderObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit NestedRenderObject(flux::TXContext &tx, kira::Properties const &) : RenderObject(tx) {
        childId = tx.create<TestRenderObject>(kira::Properties{})->getContextId();
    }

public:
    std::size_t childId{};
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

class ThrowingConstructor final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit ThrowingConstructor(flux::TXContext &tx, kira::Properties const &) : RenderObject(tx) {
        (void)tx.create<TrackedRenderObject>(kira::Properties{});
        throw std::runtime_error("constructor failed");
    }
};

class ThrowingRegistration final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit ThrowingRegistration(flux::TXContext &tx, kira::Properties const &)
        : RenderObject(tx) {
        (void)tx.create<TrackedRenderObject>(kira::Properties{});
        ++liveCount;
    }

public:
    ~ThrowingRegistration() override { --liveCount; }

    static inline int liveCount{0};

protected:
    void registerTo(flux::TXContext &tx) override {
        RenderObject::registerTo(tx);
        throw std::runtime_error("registration failed");
    }
};

} // namespace

TEST(ContextTests, OwnsCreatedObjectsAndConsumesProperties) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("answer", 42);

    auto object = context->create<TestRenderObject>(properties);
    auto const id = object->getContextId();
    auto *rawObject = object.get();

    EXPECT_EQ(object->getContext(), context.get());
    EXPECT_EQ(object->answer, 42);
    EXPECT_EQ(context->getNumContextObjects(), 1);
    EXPECT_EQ(context->get<TestRenderObject>(id), object);
    auto constObject = std::as_const(*context).get<TestRenderObject>(id);
    static_assert(std::is_same_v<decltype(constObject->getContext()), flux::Context const *>);
    EXPECT_EQ(constObject.get(), rawObject);
    EXPECT_EQ(constObject->getContext(), std::as_const(context).get());

    object.reset();
    EXPECT_EQ(context->get<TestRenderObject>(id).get(), rawObject);
}

TEST(ContextTests, ClearsOwnerWhenContextIsDestroyed) {
    auto context = flux::Context::create();
    auto object = context->create<TestRenderObject>(kira::Properties{});

    context.reset();

    EXPECT_EQ(object->getContext(), nullptr);
}

TEST(ContextTests, AbsorbsNestedCreationAsOneTransaction) {
    auto context = flux::Context::create();
    auto parent = context->create<NestedRenderObject>(kira::Properties{});

    EXPECT_EQ(context->getNumContextObjects(), 2);
    EXPECT_NE(parent->getContextId(), parent->childId);
    EXPECT_EQ(context->get<TestRenderObject>(parent->childId)->getContext(), context.get());
}

TEST(ContextTests, RollsBackConstructionAndRegistrationFailures) {
    auto context = flux::Context::create();

    EXPECT_THROW(
        (void)context->create<ThrowingConstructor>(kira::Properties{}), std::runtime_error
    );
    EXPECT_EQ(context->getNumContextObjects(), 0);
    EXPECT_EQ(TrackedRenderObject::liveCount, 0);

    EXPECT_THROW(
        (void)context->create<ThrowingRegistration>(kira::Properties{}), std::runtime_error
    );
    EXPECT_EQ(context->getNumContextObjects(), 0);
    EXPECT_EQ(TrackedRenderObject::liveCount, 0);
    EXPECT_EQ(ThrowingRegistration::liveCount, 0);
    EXPECT_NO_THROW(context->commit());
}

TEST(ContextTests, RejectsUnknownIdsAndWrongTypes) {
    auto context = flux::Context::create();
    auto object = context->create<TestRenderObject>(kira::Properties{});

    EXPECT_THROW(
        (void)context->get<TestRenderObject>(object->getContextId() + 1), std::out_of_range
    );
    EXPECT_THROW((void)context->get<OtherRenderObject>(object->getContextId()), kira::Anyhow);
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

TEST(ContextIndexMapTests, RejectsConflictingTransactionAtomically) {
    flux::ContextIndexMap indices;
    flux::ContextIndexMap::Transaction first;
    first.insert(10);
    indices.merge(std::move(first));

    flux::ContextIndexMap::Transaction conflicting;
    conflicting.insert(10);
    conflicting.insert(20);

    EXPECT_THROW(indices.merge(std::move(conflicting)), kira::Anyhow);
    EXPECT_EQ(indices.size(), 1);
    EXPECT_THROW((void)indices.getIndex(20), std::out_of_range);
}

TEST(ContextTests, TracksBSDFAndEDFIndices) {
    auto context = flux::Context::create();
    kira::Properties bsdfProps;
    bsdfProps.set("type", "diffuse");

    auto firstBSDF = context->create<flux::BSDF>(bsdfProps);
    auto secondBSDF = context->create<flux::BSDF>(bsdfProps);
    auto edf = context->create<flux::EDF>(kira::Properties{});

    EXPECT_EQ(context->getIndex<flux::BSDF>(firstBSDF->getContextId()), 0);
    EXPECT_EQ(context->getIndex<flux::BSDF>(secondBSDF->getContextId()), 1);
    EXPECT_EQ(context->getIndex<flux::EDF>(edf->getContextId()), 0);
    EXPECT_THROW((void)context->getIndex<flux::EDF>(firstBSDF->getContextId()), std::out_of_range);
}

TEST(ContextTests, ObjectsReportTheirIndexedKind) {
    static_assert(flux::BSDF::indexedKind == flux::IndexedKind::BSDF);
    static_assert(flux::EDF::indexedKind == flux::IndexedKind::EDF);
    static_assert(flux::ImageTexture::indexedKind == flux::IndexedKind::ImageTexture);
    static_assert(flux::Geometry::indexedKind == flux::IndexedKind::Geometry);
    static_assert(flux::Primitive::indexedKind == flux::IndexedKind::Primitive);
    static_assert(flux::IsIndexedObject<flux::DiffuseBSDF>);
    static_assert(!flux::IsIndexedObject<TestRenderObject>);

    auto context = flux::Context::create();
    kira::Properties bsdfProps;
    bsdfProps.set("type", "diffuse");
    auto bsdf = context->create<flux::BSDF>(bsdfProps);
    auto edf = context->create<flux::EDF>(kira::Properties{});
    auto plain = context->create<TestRenderObject>(kira::Properties{});

    // The override agrees with the static trait; unindexed objects have no kind.
    EXPECT_EQ(bsdf->getIndexedKind(), flux::BSDF::indexedKind);
    EXPECT_EQ(edf->getIndexedKind(), flux::EDF::indexedKind);
    EXPECT_FALSE(plain->getIndexedKind().has_value());

    // Only an indexed object enters the map of its kind.
    EXPECT_EQ(context->getIndexLimit<flux::BSDF>(), 1);
    EXPECT_EQ(context->getIndexLimit<flux::EDF>(), 1);
    EXPECT_EQ(context->getIndexLimit<flux::ImageTexture>(), 0);
    EXPECT_EQ(context->getObjects<flux::BSDF>().size(), 1);
    EXPECT_EQ(context->getObjects<flux::DiffuseBSDF>().size(), 1);
    EXPECT_EQ(context->getObjects<flux::PrincipledBSDF>().size(), 0);
}
