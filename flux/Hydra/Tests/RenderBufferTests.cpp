#include <gtest/gtest.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/usd/sdf/path.h>

#include <vector>

#include "flux/Hydra/RenderBuffer.h"

namespace {
std::vector<float> read(HdFluxRenderBuffer &buffer, std::size_t floatCount) {
    auto const *const data = static_cast<float const *>(buffer.Map());
    std::vector<float> values(data, data + floatCount);
    buffer.Unmap();
    return values;
}

// The rows of a 2x2 image, top row first.
std::vector<flux::Vec3f> const image{
    {1.0F, 2.0F, 3.0F}, {4.0F, 5.0F, 6.0F}, {7.0F, 8.0F, 9.0F}, {10.0F, 11.0F, 12.0F}
};
} // namespace

TEST(RenderBufferTests, WritesTheBottomRowFirstAndAnOpaqueFourthComponent) {
    HdFluxRenderBuffer buffer{pxr::SdfPath("/aov")};
    ASSERT_TRUE(buffer.Allocate({2, 2, 1}, pxr::HdFormatFloat32Vec4, false));

    buffer.Write(image);

    EXPECT_EQ(
        read(buffer, 16), (std::vector<float>{7, 8, 9, 1, 10, 11, 12, 1, 1, 2, 3, 1, 4, 5, 6, 1})
    );
}

TEST(RenderBufferTests, WritesOnlyTheComponentsOfTheFormat) {
    HdFluxRenderBuffer buffer{pxr::SdfPath("/aov")};
    ASSERT_TRUE(buffer.Allocate({2, 2, 1}, pxr::HdFormatFloat32Vec2, false));

    buffer.Write(image);

    EXPECT_EQ(read(buffer, 8), (std::vector<float>{7, 8, 10, 11, 1, 2, 4, 5}));
}

TEST(RenderBufferTests, FillsAFloatBufferWithAScalarClearValue) {
    HdFluxRenderBuffer buffer{pxr::SdfPath("/aov")};
    ASSERT_TRUE(buffer.Allocate({2, 2, 1}, pxr::HdFormatFloat32, false));

    buffer.Clear(pxr::VtValue{1.0F});

    EXPECT_EQ(read(buffer, 4), (std::vector<float>{1, 1, 1, 1}));
}

TEST(RenderBufferTests, KeepsThePixelsAndTheStateWhenAllocatedAgainAtTheSameSize) {
    HdFluxRenderBuffer buffer{pxr::SdfPath("/aov")};
    ASSERT_TRUE(buffer.Allocate({2, 2, 1}, pxr::HdFormatFloat32Vec4, false));
    buffer.Write(image);
    buffer.SetConverged(true);

    ASSERT_TRUE(buffer.Allocate({2, 2, 1}, pxr::HdFormatFloat32Vec4, false));

    EXPECT_EQ(
        read(buffer, 16), (std::vector<float>{7, 8, 9, 1, 10, 11, 12, 1, 1, 2, 3, 1, 4, 5, 6, 1})
    );
    EXPECT_TRUE(buffer.IsConverged());
}

TEST(RenderBufferTests, ClearsTheConvergedStateWhenAllocatedAgainWithAnotherFormat) {
    HdFluxRenderBuffer buffer{pxr::SdfPath("/aov")};
    ASSERT_TRUE(buffer.Allocate({2, 2, 1}, pxr::HdFormatFloat32Vec4, false));
    buffer.Write(image);
    buffer.SetConverged(true);

    ASSERT_TRUE(buffer.Allocate({2, 2, 1}, pxr::HdFormatFloat32Vec3, false));

    EXPECT_FALSE(buffer.IsConverged());
}
