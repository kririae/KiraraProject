#include <gtest/gtest.h>

#include <string>

#include "kira/SmallVector.h"

using namespace kira;

namespace {

// More bytes than any address space holds, so the allocation fails.
constexpr size_t kUnallocatable = size_t(1) << 50;

// Not trivially copyable, so growth takes the element-wise path. The size makes a request of
// 2^31 elements unallocatable while the count still fits the 32-bit size type.
struct LargeElement {
    std::string Name;
    char Padding[1 << 20];
};
static_assert(!std::is_trivially_copyable_v<LargeElement>);

} // namespace

TEST(SmallVectorTests, GrowPodThrowsBadAllocAndKeepsInlineElements) {
    SmallVector<char, 4> vector = {'a', 'b', 'c'};
    EXPECT_THROW(vector.reserve(kUnallocatable), std::bad_alloc);
    ASSERT_EQ(vector.size(), 3u);
    EXPECT_EQ(vector.capacity(), 4u);
    EXPECT_EQ(std::string(vector.begin(), vector.end()), "abc");
}

TEST(SmallVectorTests, GrowPodThrowsBadAllocAndKeepsHeapElements) {
    SmallVector<char, 2> vector;
    for (char c = 'a'; c <= 'h'; ++c)
        vector.push_back(c);
    size_t const capacity = vector.capacity();
    ASSERT_GT(capacity, 2u);
    EXPECT_THROW(vector.reserve(kUnallocatable), std::bad_alloc);
    EXPECT_EQ(vector.capacity(), capacity);
    EXPECT_EQ(std::string(vector.begin(), vector.end()), "abcdefgh");
    vector.push_back('i');
    EXPECT_EQ(std::string(vector.begin(), vector.end()), "abcdefghi");
}

TEST(SmallVectorTests, GrowThrowsBadAllocAndKeepsElements) {
    SmallVector<LargeElement, 1> vector;
    vector.emplace_back().Name = "first";
    EXPECT_THROW(vector.reserve(size_t(1) << 31), std::bad_alloc);
    ASSERT_EQ(vector.size(), 1u);
    EXPECT_EQ(vector[0].Name, "first");
}
