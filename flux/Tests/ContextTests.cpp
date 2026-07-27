#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "flux/Scene/Context.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Scene/TXContext.h"

namespace {
class TestRenderObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit TestRenderObject(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {}

public:
    void link() override { ++linkCount; }

    int linkCount{0};
};

class OtherRenderObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit OtherRenderObject(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {}
};

class NestedRenderObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit NestedRenderObject(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {
        childId = tx.create<TestRenderObject>()->getContextId();
    }

public:
    std::size_t childId{};
};

class TrackedRenderObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit TrackedRenderObject(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {
        ++liveCount;
    }

public:
    ~TrackedRenderObject() override { --liveCount; }

    static inline int liveCount{0};
};

class ThrowingConstructor final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit ThrowingConstructor(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {
        (void)tx.create<TrackedRenderObject>();
        throw std::runtime_error("constructor failed");
    }
};

class ThrowingRegistration final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit ThrowingRegistration(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {
        (void)tx.create<TrackedRenderObject>();
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

class RetryLinkObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit RetryLinkObject(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {}

public:
    void link() override {
        ++linkCount;
        if (linkCount == 1)
            throw std::runtime_error("link failed");
    }

    int linkCount{0};
};

class CreatesDuringLink final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit CreatesDuringLink(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {}

public:
    void link() override { (void)getContext()->create<TestRenderObject>(); }
};

class CommitsDuringLink final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit CommitsDuringLink(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {}

public:
    void link() override { getContext()->commit(); }
};
} // namespace

TEST(ContextTests, OwnsCreatedObjectsAndProperties) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("answer", 42);

    auto object = context->create<TestRenderObject>(std::move(properties));
    auto const id = object->getContextId();
    auto *rawObject = object.get();

    EXPECT_EQ(object->getContext(), context.get());
    EXPECT_EQ(object->getProperties().get<int>("answer"), 42);
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
    auto object = context->create<TestRenderObject>();

    context.reset();

    EXPECT_EQ(object->getContext(), nullptr);
}

TEST(ContextTests, AbsorbsNestedCreationAsOneTransaction) {
    auto context = flux::Context::create();
    auto parent = context->create<NestedRenderObject>();

    EXPECT_EQ(context->getNumContextObjects(), 2);
    EXPECT_NE(parent->getContextId(), parent->childId);
    EXPECT_EQ(context->get<TestRenderObject>(parent->childId)->getContext(), context.get());
}

TEST(ContextTests, RollsBackConstructionAndRegistrationFailures) {
    auto context = flux::Context::create();

    EXPECT_THROW((void)context->create<ThrowingConstructor>(), std::runtime_error);
    EXPECT_EQ(context->getNumContextObjects(), 0);
    EXPECT_EQ(TrackedRenderObject::liveCount, 0);

    EXPECT_THROW((void)context->create<ThrowingRegistration>(), std::runtime_error);
    EXPECT_EQ(context->getNumContextObjects(), 0);
    EXPECT_EQ(TrackedRenderObject::liveCount, 0);
    EXPECT_EQ(ThrowingRegistration::liveCount, 0);
    EXPECT_NO_THROW(context->commit());
}

TEST(ContextTests, LinksEachSuccessfulBatchOnce) {
    auto context = flux::Context::create();
    auto object = context->create<TestRenderObject>();

    context->commit();
    EXPECT_EQ(object->linkCount, 1);

    context->commit();
    EXPECT_EQ(object->linkCount, 1);
}

TEST(ContextTests, RetriesTheBatchAfterLinkFailure) {
    auto context = flux::Context::create();
    auto object = context->create<RetryLinkObject>();

    EXPECT_THROW(context->commit(), std::runtime_error);
    EXPECT_EQ(object->linkCount, 1);

    EXPECT_NO_THROW(context->commit());
    EXPECT_EQ(object->linkCount, 2);

    context->commit();
    EXPECT_EQ(object->linkCount, 2);
}

TEST(ContextTests, RejectsCreationDuringCommit) {
    auto context = flux::Context::create();
    (void)context->create<CreatesDuringLink>();

    EXPECT_THROW(context->commit(), kira::Anyhow);
    EXPECT_EQ(context->getNumContextObjects(), 1);
}

TEST(ContextTests, RejectsRecursiveCommit) {
    auto context = flux::Context::create();
    (void)context->create<CommitsDuringLink>();

    EXPECT_THROW(context->commit(), kira::Anyhow);
}

TEST(ContextTests, RejectsUnknownIdsAndWrongTypes) {
    auto context = flux::Context::create();
    auto object = context->create<TestRenderObject>();

    EXPECT_THROW(
        (void)context->get<TestRenderObject>(object->getContextId() + 1), std::out_of_range
    );
    EXPECT_THROW((void)context->get<OtherRenderObject>(object->getContextId()), kira::Anyhow);
}
