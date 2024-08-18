#include <gtest/gtest.h>

#include <cstddef>
#include <random>
#include <span>

#include "kira/Vecteur.h"

#include "ArithLib.h"
#include "VecteurTests.h"

using namespace kira;

class VecteurDynamicTests : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

protected:
    static constexpr std::size_t rtsize{32};

    template <is_vecteur T> [[nodiscard]] T RandDynamicVecteur(size_t size) const {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> valDis(0.0f, 100.0f);

        T result(size);
        for (std::size_t i = 0; i < size; ++i)
            result[i] = valDis(gen);
        return result;
    }
};

TEST_F(VecteurDynamicTests, DefaultConstruct) {
    InstantiateDynamicTests<int>([]<typename Vecteur>() {
        Vecteur v;
        EXPECT_EQ(v.size(), 0);
    });
}

TEST_F(VecteurDynamicTests, ConstructWithSize) {
    InstantiateDynamicTests<int>([]<typename Vecteur>() {
        Vecteur v1(10);
        EXPECT_EQ(v1.size(), 10);

        Vecteur v2{10};
        EXPECT_EQ(v2.size(), 1);
        EXPECT_EQ(v2[0], 10);

        Vecteur v3{10, 11};
        EXPECT_EQ(v3.size(), 2);
        EXPECT_EQ(v3[0], 10);
        EXPECT_EQ(v3[1], 11);

        Vecteur v4(10, 11);
        EXPECT_EQ(v4.size(), 10);
        for (auto i = 0; i < 10; ++i)
            EXPECT_EQ(v4[i], 11);
    });
}

TEST_F(VecteurDynamicTests, CompatibleWithStatic) {
    InstantiateStaticTests<int, 3>([]<typename Vecteur>() {
        Vecteur v1(10);
        EXPECT_EQ(v1.size(), 3);
        EXPECT_EQ(v1[0], 10);
        EXPECT_EQ(v1[1], 10);
        EXPECT_EQ(v1[2], 10);

        Vecteur v2{10};
        EXPECT_EQ(v2.size(), 3);
        EXPECT_EQ(v2[0], 10);
        EXPECT_EQ(v2[1], 10);
        EXPECT_EQ(v2[2], 10);
    });

    InstantiateStaticTests<int, 2>([]<typename Vecteur>() {
        Vecteur v3{10, 11};
        EXPECT_EQ(v3.size(), 2);
        EXPECT_EQ(v3[0], 10);
        EXPECT_EQ(v3[1], 11);

        Vecteur v4(10, 11);
        EXPECT_EQ(v4.size(), 2);
        EXPECT_EQ(v4[0], 10);
        EXPECT_EQ(v4[1], 11);
    });
}

TEST_F(VecteurDynamicTests, ConstructWithConversion) {
    InstantiateDynamicTests<float>([]<typename Vecteur>() {
        Vecteur v1(10);
        EXPECT_EQ(v1.size(), 10);

        Vecteur v2{10};
        EXPECT_EQ(v2.size(), 1);
        EXPECT_FLOAT_EQ(v2[0], 10);

        Vecteur v3{10, 11};
        EXPECT_EQ(v3.size(), 2);
        EXPECT_FLOAT_EQ(v3[0], 10);
        EXPECT_FLOAT_EQ(v3[1], 11);

        Vecteur v4(10, 11);
        EXPECT_EQ(v4.size(), 10);
        EXPECT_FLOAT_EQ(v4[0], 11);
        EXPECT_FLOAT_EQ(v4[1], 11);
    });
}

TEST_F(VecteurDynamicTests, CopyConstruct) {
    InstantiateDynamicTests<int>([]<typename Vecteur>() {
        Vecteur v1(10, 11);
        Vecteur v2(v1);
        EXPECT_EQ(v2.size(), 10);
        EXPECT_NE(v1.data(), v2.data());
        for (auto i = 0; i < 10; ++i)
            EXPECT_EQ(v2[i], 11);
    });
}

TEST_F(VecteurDynamicTests, MoveConstruct) {
    InstantiateDynamicTests<int>([]<typename Vecteur>() {
        Vecteur v1(10, 11);
        auto *ptr = v1.data();
        Vecteur v2(std::move(v1));
        EXPECT_EQ(ptr, v2.data());
        EXPECT_EQ(v2.size(), 10);
        for (auto i = 0; i < 10; ++i)
            EXPECT_EQ(v2[i], 11);
    });
}

TEST_F(VecteurDynamicTests, ConstructFromArray) {
    InstantiateDynamicTests<int>([]<typename Vecteur>() {
        std::array<int, 3> arr{10, 11, 12};
        Vecteur v(arr);
        EXPECT_EQ(v.size(), 3);
        for (auto i = 0; i < 3; ++i)
            EXPECT_EQ(v[i], arr[i]);
    });
}

TEST_F(VecteurDynamicTests, ConstructFromOtherArray) {
    InstantiateDynamicTests<float>([]<typename Vecteur>() {
        std::array<int, 3> arr{10, 11, 12};
        Vecteur v(arr);
        EXPECT_EQ(v.size(), 3);
        for (auto i = 0; i < 3; ++i)
            EXPECT_FLOAT_EQ(v[i], arr[i]);
    });
}

TEST_F(VecteurDynamicTests, ConstructFromSpan) {
    InstantiateDynamicTests<int>([]<typename Vecteur>() {
        std::array<int, 3> arr{10, 11, 12};
        std::span<int, 3> span(arr);
        Vecteur v(span);
        EXPECT_EQ(v.size(), 3);
        for (auto i = 0; i < 3; ++i)
            EXPECT_EQ(v[i], arr[i]);
    });
}

TEST_F(VecteurDynamicTests, ConstructFromOtherSpan) {
    InstantiateDynamicTests<float>([]<typename Vecteur>() {
        std::array<int, 3> arr{10, 11, 12};
        std::span<int, 3> span(arr);
        Vecteur v(span);
        EXPECT_EQ(v.size(), 3);
        for (auto i = 0; i < 3; ++i)
            EXPECT_FLOAT_EQ(v[i], arr[i]);
    });
}

TEST_F(VecteurDynamicTests, ConstructFromDynamicSpan) {
    InstantiateDynamicTests<int>([]<typename Vecteur>() {
        std::array<int, 3> arr{10, 11, 12};
        std::span<int, std::dynamic_extent> span(arr);
        Vecteur v(span);
        EXPECT_EQ(v.size(), 3);
        for (auto i = 0; i < 3; ++i)
            EXPECT_EQ(v[i], arr[i]);
    });
}

TEST_F(VecteurDynamicTests, ConstructFromOtherDynamicSpan) {
    InstantiateDynamicTests<float>([]<typename Vecteur>() {
        std::array<int, 3> arr{10, 11, 12};
        std::span<int, std::dynamic_extent> span(arr);
        Vecteur v(span);
        EXPECT_EQ(v.size(), 3);
        for (auto i = 0; i < 3; ++i)
            EXPECT_FLOAT_EQ(v[i], arr[i]);
    });
}

TEST_F(VecteurDynamicTests, ConstructFromStatic) {
    Vecteur<int, 3, VecteurBackend::Generic> v1(10, 11, 12);
    InstantiateDynamicTests<int>([&]<typename Vecteur>() {
        Vecteur v2(v1);
        EXPECT_EQ(v2.size(), 3);
        for (auto i = 0; i < 3; ++i)
            EXPECT_EQ(v2[i], v1[i]);
    });
}

TEST_F(VecteurDynamicTests, ConstructFromOtherStatic) {
    Vecteur<int, 3, VecteurBackend::Generic> v1(10, 11, 12);
    InstantiateDynamicTests<float>([&]<typename Vecteur>() {
        Vecteur v2(v1);
        EXPECT_EQ(v2.size(), 3);
        EXPECT_FLOAT_EQ(10, v2[0]);
        EXPECT_FLOAT_EQ(11, v2[1]);
        EXPECT_FLOAT_EQ(12, v2[2]);
    });
}

// Assignment from
// [{}|Move] x [{}|other] x [Static|Dynamic] x [{}|Less|More]
// 18 cases
// (1) AssignmentFromStatic               (y)
// (2) AssignmentFromOtherStatic          (y)
// (3) AssignmentFromStaticLess           (not necessary)
// (4) AssignmentFromOtherStaticLess      (not necessary)
// (5) AssignmentFromStaticMore           (not necessary)
// (6) AssignmentFromOtherStaticMore      (not necessary)
// (7) AssignmentFromDynamic              (y)
// (8) AssignmentFromOtherDynamic         (y)
// (9) AssignmentFromDynamicLess          (y)
// (10) AssignmentFromOtherDynamicLess    (y)
// (11) AssignmentFromDynamicMore         (y)
// (12) AssignmentFromOtherDynamicMore    (y)
// (13) MoveAssignmentFromStatic          (not necessary)
// (14) MoveAssignmentFromOtherStatic     (not necessary)
// (15) MoveAssignmentFromDynamic         (y)
// (16) MoveAssignmentFromOtherDynamic    (y)
//
// (17) AssignmentFromSelf                (y)
// (18) MoveAssignmentFromSelf            (y)

TEST_F(VecteurDynamicTests, AssignmentFromStatic) {
    Vecteur<int, 3, VecteurBackend::Generic> v1(10, 11, 12);
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v2;
    auto *ptr = v2.data();
    v2 = v1;
    auto *ptr2 = v2.data();
    EXPECT_NE(ptr, ptr2);
    EXPECT_EQ(v2.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v2[i], v1[i]);
}

TEST_F(VecteurDynamicTests, AssignmentFromOtherStatic) {
    Vecteur<int, 3, VecteurBackend::Generic> v1(10, 11, 12);
    Vecteur<float, std::dynamic_extent, VecteurBackend::Generic> v2;
    v2 = v1;
    EXPECT_EQ(v2.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_FLOAT_EQ(v2[i], v1[i]);
}

TEST_F(VecteurDynamicTests, AssignmentFromDynamicSameSize) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v1{10, 11, 12};
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v2{13, 14, 15};
    auto *ptr = v1.data();
    v1 = v2;
    auto *ptr2 = v1.data();
    EXPECT_EQ(ptr, ptr2);
    EXPECT_EQ(v1.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v1[i], v2[i]);
}

TEST_F(VecteurDynamicTests, AssignmentFromOtherDynamicSameSize) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v1{10, 11, 12};
    Vecteur<float, std::dynamic_extent, VecteurBackend::Generic> v2{10, 11, 12};
    v2 = v1;

    EXPECT_EQ(v2.size(), 3);
    EXPECT_FLOAT_EQ(v2[0], 10);
    EXPECT_FLOAT_EQ(v2[1], 11);
    EXPECT_FLOAT_EQ(v2[2], 12);
}

TEST_F(VecteurDynamicTests, AssignemtFromDynamicLessSize) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v1{10, 11, 12};
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v2{13, 14};
    auto *ptr = v1.data();
    v1 = v2;
    auto *ptr2 = v1.data();
    EXPECT_NE(ptr, ptr2);
    EXPECT_EQ(v1.size(), 2);
    for (auto i = 0; i < 2; ++i)
        EXPECT_EQ(v1[i], v2[i]);
}

TEST_F(VecteurDynamicTests, AssignmentFromOtherDynamicLessSize) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v1{10, 11, 12};
    Vecteur<float, std::dynamic_extent, VecteurBackend::Generic> v2{13, 14};
    v2 = v1;

    EXPECT_EQ(v2.size(), 3);
    EXPECT_FLOAT_EQ(v2[0], 10);
    EXPECT_FLOAT_EQ(v2[1], 11);
    EXPECT_FLOAT_EQ(v2[2], 12);
}

TEST_F(VecteurDynamicTests, AssignmentFromDynamicMoreSize) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v1{10, 11};
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v2{13, 14, 15};
    auto *ptr = v1.data();
    v1 = v2;
    auto *ptr2 = v1.data();
    EXPECT_NE(ptr, ptr2);
    EXPECT_EQ(v1.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v1[i], v2[i]);
}

TEST_F(VecteurDynamicTests, AssignmentFromOtherDynamicMoreSize) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v1{10, 11};
    Vecteur<float, std::dynamic_extent, VecteurBackend::Generic> v2{13, 14, 15};
    v2 = v1;

    EXPECT_EQ(v2.size(), 2);
    EXPECT_FLOAT_EQ(v2[0], 10);
    EXPECT_FLOAT_EQ(v2[1], 11);
}

TEST_F(VecteurDynamicTests, AssignmentFromSelf) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v1{10, 11, 12};
    auto *ptr = v1.data();
    v1 = v1;
    auto *ptr2 = v1.data();
    EXPECT_EQ(ptr, ptr2);
    EXPECT_EQ(v1.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v1[i], 10 + i);
}

TEST_F(VecteurDynamicTests, MoveAssignmentFromSelf) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v1{10, 11, 12};
    auto *ptr = v1.data();
    v1 = std::move(v1);
    auto *ptr2 = v1.data();
    EXPECT_EQ(ptr, ptr2);
    EXPECT_EQ(v1.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v1[i], 10 + i);
}

TEST_F(VecteurDynamicTests, MoveAssignmentFromDynamic) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v1{10, 11, 12};
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v2{13, 14, 15};
    auto *ptr = v2.data();
    v1 = std::move(v2);
    auto *ptr2 = v1.data();
    EXPECT_EQ(ptr, ptr2);
    EXPECT_EQ(v1.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v1[i], 13 + i);
}

TEST_F(VecteurDynamicTests, MoveAssignmentFromDynamicDifferentSize) {
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v1{10, 11, 12};
    Vecteur<int, std::dynamic_extent, VecteurBackend::Generic> v2{13, 14};
    auto *ptr = v2.data();
    v1 = std::move(v2);
    auto *ptr2 = v1.data();
    EXPECT_EQ(ptr, ptr2);
    EXPECT_EQ(v1.size(), 2);
    for (auto i = 0; i < 2; ++i)
        EXPECT_EQ(v1[i], 13 + i);
}

TEST_F(VecteurDynamicTests, DontConstructStaticFromDynamic) {
    static_assert(
        !std::is_constructible_v<
            Vecteur<int, 3, VecteurBackend::Generic>,
            Vecteur<int, std::dynamic_extent, VecteurBackend::Generic>>,
        "Static vecteur should not be constructed from dynamic vecteur."
    );
}

TEST_F(VecteurDynamicTests, Addition) {
    InstantiateDynamicTests<float>([&]<typename Vecteur>() {
        auto v1 = RandDynamicVecteur<Vecteur>(rtsize);
        auto v2 = RandDynamicVecteur<Vecteur>(rtsize);

        Vecteur v3 = v1 + v2;
        for (std::size_t i = 0; i < rtsize; ++i)
            EXPECT_FLOAT_EQ(v3[i], v1[i] + v2[i]);

        Vecteur v4 = v1 + 1;
        for (std::size_t i = 0; i < rtsize; ++i)
            EXPECT_FLOAT_EQ(v4[i], v1[i] + 1);

        Vecteur v5 = 1.1F + v2;
        for (std::size_t i = 0; i < rtsize; ++i)
            EXPECT_FLOAT_EQ(v5[i], 1.1F + v2[i]);
    });
}

TEST_F(VecteurDynamicTests, Multiplication) {
    InstantiateDynamicTests<float>([&]<typename Vecteur>() {
        auto v1 = RandDynamicVecteur<Vecteur>(rtsize);
        auto v2 = RandDynamicVecteur<Vecteur>(rtsize);

        Vecteur v3 = v1 * v2;
        for (std::size_t i = 0; i < rtsize; ++i)
            EXPECT_FLOAT_EQ(v3[i], v1[i] * v2[i]);

        Vecteur v4 = v1 * 1.1F;
        for (std::size_t i = 0; i < rtsize; ++i)
            EXPECT_FLOAT_EQ(v4[i], v1[i] * 1.1F);

        Vecteur v5 = 1.1F * v2;
        for (std::size_t i = 0; i < rtsize; ++i)
            EXPECT_FLOAT_EQ(v5[i], 1.1F * v2[i]);
    });
}

TEST_F(VecteurDynamicTests, Division) {
    InstantiateDynamicTests<float>([&]<typename Vecteur>() {
        auto v1 = RandDynamicVecteur<Vecteur>(rtsize);
        auto v2 = RandDynamicVecteur<Vecteur>(rtsize);

        Vecteur v3 = v1 / v2;
        for (std::size_t i = 0; i < rtsize; ++i)
            EXPECT_FLOAT_EQ(v3[i], v1[i] / v2[i]);

        Vecteur v4 = v1 / 1.1F;
        for (std::size_t i = 0; i < rtsize; ++i)
            EXPECT_FLOAT_EQ(v4[i], v1[i] / 1.1F);

        Vecteur v5 = 1.1F / v2;
        for (std::size_t i = 0; i < rtsize; ++i)
            EXPECT_FLOAT_EQ(v5[i], 1.1F / v2[i]);
    });
}

// This is an exception that clang-16 cannot compile.
#if (defined(__clang__) and (__clang_major__ >= 18)) or (defined(__GNUC__) and (__GNUC__ >= 11))
TEST_F(VecteurDynamicTests, FresnelConductor) {
    using GenericVecteur = Vecteur<float, std::dynamic_extent, VecteurBackend::Generic>;
    using LazyVecteur = Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy>;
    using DoubleLazyVecteur = Vecteur<double, std::dynamic_extent, VecteurBackend::Lazy>;

    auto etaI = RandDynamicVecteur<GenericVecteur>(rtsize);
    auto etaT = RandDynamicVecteur<GenericVecteur>(rtsize);
    auto k = RandDynamicVecteur<GenericVecteur>(rtsize);

    ASSERT_TRUE(etaI.size() == rtsize);
    ASSERT_TRUE(etaT.size() == rtsize);
    ASSERT_TRUE(k.size() == rtsize);

    auto gt = FresnelConductor(
        0.5F, DoubleLazyVecteur{etaI}, DoubleLazyVecteur{etaT}, DoubleLazyVecteur{k}
    );

    auto lazyEtaI = LazyVecteur{etaI};
    auto lazyEtaT = LazyVecteur{etaT};
    auto lazyK = LazyVecteur{k};

    ASSERT_TRUE(lazyEtaI.size() == rtsize);
    ASSERT_TRUE(lazyEtaT.size() == rtsize);
    ASSERT_TRUE(lazyK.size() == rtsize);

    auto lazyGt = FresnelConductor(0.5F, lazyEtaI, lazyEtaT, lazyK);
    auto lazyExpandedGt = FresnelConductorExpanded(0.5F, lazyEtaI, lazyEtaT, lazyK);

    for (std::size_t i = 0; i < rtsize; ++i)
        EXPECT_NEAR(gt[i], lazyGt[i], 1e-4);
    for (std::size_t i = 0; i < rtsize; ++i)
        EXPECT_NEAR(gt[i], lazyExpandedGt[i], 1e-4);
    for (std::size_t i = 0; i < rtsize; ++i)
        EXPECT_NEAR(gt[i], gt[i], 1e-4);
}
#endif
