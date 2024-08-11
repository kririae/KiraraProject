#include <gtest/gtest.h>

#include "kira/Vecteur.h"

using namespace kira;

class VecteurDynamicLazyTests : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(VecteurDynamicLazyTests, DefaultConstruct) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v;
    EXPECT_EQ(v.size(), 0);
}

TEST_F(VecteurDynamicLazyTests, ConstructWithSize) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v1(10);
    EXPECT_EQ(v1.size(), 10);

    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v2{10};
    EXPECT_EQ(v2.size(), 1);
    EXPECT_EQ(v2[0], 10);

    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v3{10, 11};
    EXPECT_EQ(v3.size(), 2);
    EXPECT_EQ(v3[0], 10);
    EXPECT_EQ(v3[1], 11);

    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v4(10, 11);
    EXPECT_EQ(v4.size(), 10);
    for (auto i = 0; i < 10; ++i)
        EXPECT_EQ(v4[i], 11);
}

TEST_F(VecteurDynamicLazyTests, CompatibleWithStatic) {
    Vecteur<int, 3, VecteurBackend::Lazy> v1(10);
    EXPECT_EQ(v1.size(), 3);

    Vecteur<int, 3, VecteurBackend::Lazy> v2{10};
    EXPECT_EQ(v2.size(), 3);

    Vecteur<int, 2, VecteurBackend::Lazy> v3{10, 11};
    EXPECT_EQ(v3.size(), 2);
    EXPECT_EQ(v3[0], 10);
    EXPECT_EQ(v3[1], 11);

    Vecteur<int, 2, VecteurBackend::Lazy> v4(10, 11);
    EXPECT_EQ(v4.size(), 2);
    EXPECT_EQ(v4[0], 10);
    EXPECT_EQ(v4[1], 11);
}

TEST_F(VecteurDynamicLazyTests, ConstructWithConversion) {
    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> v1(10);
    EXPECT_EQ(v1.size(), 10);

    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> v2{10};
    EXPECT_EQ(v2.size(), 1);
    EXPECT_FLOAT_EQ(v2[0], 10);

    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> v3{10, 11};
    EXPECT_EQ(v3.size(), 2);
    EXPECT_FLOAT_EQ(v3[0], 10);
    EXPECT_FLOAT_EQ(v3[1], 11);

    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> v4(10, 11);
    EXPECT_EQ(v4.size(), 10);
    EXPECT_FLOAT_EQ(v4[0], 11);
    EXPECT_FLOAT_EQ(v4[1], 11);
}

TEST_F(VecteurDynamicLazyTests, CopyConstruct) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v1(10, 11);
    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v2(v1);
    EXPECT_EQ(v2.size(), 10);
    for (auto i = 0; i < 10; ++i)
        EXPECT_EQ(v2[i], 11);
}

TEST_F(VecteurDynamicLazyTests, MoveConstruct) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v1(10, 11);
    auto *ptr = v1.data();
    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v2(std::move(v1));
    EXPECT_EQ(ptr, v2.data());
    EXPECT_EQ(v2.size(), 10);
    for (auto i = 0; i < 10; ++i)
        EXPECT_EQ(v2[i], 11);
}

TEST_F(VecteurDynamicLazyTests, ConstructFromArray) {
    std::array<int, 3> arr{10, 11, 12};
    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v(arr);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicLazyTests, ConstructFromOtherArray) {
    std::array<int, 3> arr{10, 11, 12};
    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> v(arr);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_FLOAT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicLazyTests, ConstructFromSpan) {
    std::array<int, 3> arr{10, 11, 12};
    std::span<int, 3> span(arr);
    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v(span);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicLazyTests, ConstructFromOtherSpan) {
    std::array<int, 3> arr{10, 11, 12};
    std::span<int, 3> span(arr);
    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> v(span);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_FLOAT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicLazyTests, ConstructFromDynamicSpan) {
    std::array<int, 3> arr{10, 11, 12};
    std::span<int, std::dynamic_extent> span(arr);
    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v(span);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicLazyTests, ConstructFromOtherDynamicSpan) {
    std::array<int, 3> arr{10, 11, 12};
    std::span<int, std::dynamic_extent> span(arr);
    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> v(span);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_FLOAT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicLazyTests, ConstructFromStatic) {
    Vecteur<int, 3, VecteurBackend::Lazy> v1(10, 11, 12);
    Vecteur<int, std::dynamic_extent, VecteurBackend::Lazy> v2(v1);
    EXPECT_EQ(v2.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v2[i], v1[i]);
}

TEST_F(VecteurDynamicLazyTests, ConstructFromOtherStatic) {
    Vecteur<int, 3, VecteurBackend::Lazy> v1(10, 11, 12);
    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> v2(v1);

    EXPECT_EQ(v2.size(), 3);
    EXPECT_FLOAT_EQ(10, v2[0]);
    EXPECT_FLOAT_EQ(11, v2[1]);
    EXPECT_FLOAT_EQ(12, v2[2]);
}
