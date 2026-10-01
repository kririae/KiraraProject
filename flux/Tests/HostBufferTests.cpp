#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "flux/Core/HostBuffer.h"
#include "kira/SmallVector.h"

using flux::HostBuffer;
using flux::Shared;

static_assert(!std::copy_constructible<HostBuffer<std::uint32_t>>);
static_assert(std::movable<HostBuffer<std::uint32_t>>);
static_assert(std::constructible_from<HostBuffer<std::uint32_t>, std::vector<std::uint32_t>>);
static_assert(!std::constructible_from<HostBuffer<std::uint32_t>, std::vector<std::uint32_t> &>);
static_assert(!std::constructible_from<HostBuffer<std::uint32_t>, std::vector<std::int32_t>>);
static_assert(!std::constructible_from<HostBuffer<std::uint32_t>, std::uint32_t[4]>);

TEST(HostBufferTests, DefaultIsEmptyWithNullData) {
    HostBuffer<std::uint32_t> buffer;

    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.data(), nullptr);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.capacity(), 0);
    EXPECT_TRUE(buffer.span().empty());
}

TEST(HostBufferTests, TakesAVectorWithoutCopyingElements) {
    std::vector<std::uint32_t> values{1, 2, 3};
    values.reserve(8);
    auto const *data = values.data();

    HostBuffer<std::uint32_t> buffer(std::move(values));

    EXPECT_EQ(buffer.data(), data);
    EXPECT_EQ(buffer.size(), 3);
    EXPECT_EQ(buffer.capacity(), 8);
    EXPECT_EQ(buffer.span()[2], 3);
}

TEST(HostBufferTests, TakesASmallVectorWithoutCopyingElements) {
    kira::SmallVector<std::uint32_t, 2> values;
    for (std::uint32_t value = 0; value < 6; ++value)
        values.push_back(value);
    auto const *data = values.data();
    auto const capacity = values.capacity();

    HostBuffer<std::uint32_t> buffer(std::move(values));

    EXPECT_EQ(buffer.data(), data);
    EXPECT_EQ(buffer.size(), 6);
    EXPECT_EQ(buffer.capacity(), capacity);
    EXPECT_EQ(buffer.span()[5], 5);
}

TEST(HostBufferTests, TakesAnInlineSmallVectorWithItsElements) {
    kira::SmallVector<std::uint32_t, 4> values{7, 8};

    HostBuffer<std::uint32_t> buffer(std::move(values));

    EXPECT_EQ(buffer.size(), 2);
    EXPECT_GE(buffer.capacity(), 4);
    EXPECT_EQ(buffer.span()[0], 7);
    EXPECT_EQ(buffer.span()[1], 8);
}

TEST(HostBufferTests, EmptyContainerGivesAnEmptyBuffer) {
    std::vector<std::uint32_t> values;
    values.reserve(4);

    HostBuffer<std::uint32_t> buffer(std::move(values));

    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.data(), nullptr);
    EXPECT_EQ(buffer.capacity(), 0);
}

TEST(HostBufferTests, MoveKeepsThePointerAndEmptiesTheSource) {
    HostBuffer<std::uint32_t> source;
    source.resize(4);
    auto *const data = source.data();

    HostBuffer<std::uint32_t> moved(std::move(source));
    EXPECT_EQ(moved.data(), data);
    EXPECT_EQ(moved.size(), 4);
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(source.data(), nullptr);
    EXPECT_EQ(source.capacity(), 0);

    HostBuffer<std::uint32_t> target;
    target.resize(1);
    target = std::move(moved);
    EXPECT_EQ(target.data(), data);
    EXPECT_EQ(target.size(), 4);
    EXPECT_TRUE(moved.empty());
    EXPECT_EQ(moved.data(), nullptr);
}

TEST(HostBufferTests, ResizeSetsSizeAndCapacity) {
    HostBuffer<std::uint32_t> buffer;
    buffer.resize(5, 6);

    EXPECT_NE(buffer.data(), nullptr);
    EXPECT_EQ(buffer.size(), 5);
    EXPECT_EQ(buffer.capacity(), 6);
    EXPECT_EQ(buffer.span().size(), 5);

    buffer.resize(3);
    EXPECT_EQ(buffer.size(), 3);
    EXPECT_EQ(buffer.capacity(), 3);
}

TEST(HostBufferTests, ResizeToZeroReleasesTheAllocation) {
    HostBuffer<std::uint32_t> buffer;
    buffer.resize(4);

    buffer.resize(0);
    EXPECT_EQ(buffer.data(), nullptr);
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.capacity(), 0);

    buffer.resize(4);
    buffer.resize(0, 2);
    EXPECT_EQ(buffer.data(), nullptr);
    EXPECT_EQ(buffer.capacity(), 0);

    buffer.resize(2);
    buffer.clear();
    EXPECT_EQ(buffer.data(), nullptr);
    EXPECT_TRUE(buffer.empty());
}

TEST(HostBufferTests, SharingKeepsTheDataPointer) {
    auto buffer = std::make_shared<HostBuffer<std::uint32_t>>();
    buffer->resize(4, 5);
    auto const *data = buffer->data();

    Shared<HostBuffer<std::uint32_t>> shared = std::move(buffer);

    EXPECT_EQ(buffer, nullptr);
    EXPECT_EQ(shared->data(), data);
    EXPECT_EQ(shared->size(), 4);
    EXPECT_EQ(shared->capacity(), 5);
}
