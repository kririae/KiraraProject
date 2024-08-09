#include <gtest/gtest.h>

#include <random>
#include <span>

#include "kira/VecteurGeneric.h"
#include "kira/VecteurRouter.h"

using namespace kira;

class VecteurDynamicTests : public ::testing::Test {
protected:
    void SetUp() override {
        svIn1 = RandomSVecteur();
        svIn2 = RandomSVecteur();
        dvIn1 = svIn1;
        dvIn2 = svIn2;
    }

    void TearDown() override {
        svOut = SVecteurT{0};
        dvOut = DVecteurT(rtsize);
    }

protected:
    static constexpr std::size_t rtsize{32};

    using SVecteurT = Vecteur<float, rtsize>;
    using DVecteurT = Vecteur<float, std::dynamic_extent>;

    SVecteurT svIn1;
    SVecteurT svIn2;
    Vecteur<float, 3> svIn3{1, 2, 3};
    Vecteur<float, 3> svIn4{2, 3, 4};
    DVecteurT dvIn1;
    DVecteurT dvIn2;

    SVecteurT svOut;
    DVecteurT dvOut;

    [[nodiscard]] SVecteurT RandomSVecteur() const {
        SVecteurT result;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(-100.0f, 100.0f);

        for (std::size_t i = 0; i < rtsize; ++i)
            result[i] = dis(gen);

        return result;
    }

    [[nodiscard]] DVecteurT RandomDVecteur() const {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> sizeDis(1, 100);
        std::uniform_real_distribution<float> valDis(-100.0f, 100.0f);

        std::size_t size = sizeDis(gen);
        DVecteurT result(size);

        for (std::size_t i = 0; i < size; ++i)
            result[i] = valDis(gen);

        return result;
    }
};

TEST_F(VecteurDynamicTests, DefaultConstruct) {
    Vecteur<int, std::dynamic_extent> v;
    EXPECT_EQ(v.size(), 0);
}

TEST_F(VecteurDynamicTests, ConstructWithSize) {
    Vecteur<int, std::dynamic_extent> v1(10);
    EXPECT_EQ(v1.size(), 10);

    Vecteur<int, std::dynamic_extent> v2{10};
    EXPECT_EQ(v2.size(), 1);
    EXPECT_EQ(v2[0], 10);

    Vecteur<int, std::dynamic_extent> v3{10, 11};
    EXPECT_EQ(v3.size(), 2);
    EXPECT_EQ(v3[0], 10);
    EXPECT_EQ(v3[1], 11);

    Vecteur<int, std::dynamic_extent> v4(10, 11);
    EXPECT_EQ(v4.size(), 10);
    for (auto i = 0; i < 10; ++i)
        EXPECT_EQ(v4[i], 11);
}

TEST_F(VecteurDynamicTests, CompatibleWithStatic) {
    Vecteur<int, 3> v1(10);
    EXPECT_EQ(v1.size(), 3);

    Vecteur<int, 3> v2{10};
    EXPECT_EQ(v2.size(), 3);

    Vecteur<int, 2> v3{10, 11};
    EXPECT_EQ(v3.size(), 2);
    EXPECT_EQ(v3[0], 10);
    EXPECT_EQ(v3[1], 11);

    Vecteur<int, 2> v4(10, 11);
    EXPECT_EQ(v4.size(), 2);
    EXPECT_EQ(v4[0], 10);
    EXPECT_EQ(v4[1], 11);
}

TEST_F(VecteurDynamicTests, ConstructWithConversion) {
    Vecteur<float, std::dynamic_extent> v1(10);
    EXPECT_EQ(v1.size(), 10);

    Vecteur<float, std::dynamic_extent> v2{10};
    EXPECT_EQ(v2.size(), 1);
    EXPECT_FLOAT_EQ(v2[0], 10);

    Vecteur<float, std::dynamic_extent> v3{10, 11};
    EXPECT_EQ(v3.size(), 2);
    EXPECT_FLOAT_EQ(v3[0], 10);
    EXPECT_FLOAT_EQ(v3[1], 11);

    Vecteur<float, std::dynamic_extent> v4(10, 11);
    EXPECT_EQ(v4.size(), 10);
    EXPECT_FLOAT_EQ(v4[0], 11);
    EXPECT_FLOAT_EQ(v4[1], 11);
}

TEST_F(VecteurDynamicTests, CopyConstruct) {
    Vecteur<int, std::dynamic_extent> v1(10, 11);
    Vecteur<int, std::dynamic_extent> v2(v1);
    EXPECT_EQ(v2.size(), 10);
    for (auto i = 0; i < 10; ++i)
        EXPECT_EQ(v2[i], 11);
}

TEST_F(VecteurDynamicTests, MoveConstruct) {
    Vecteur<int, std::dynamic_extent> v1(10, 11);
    auto *ptr = v1.data();
    Vecteur<int, std::dynamic_extent> v2(std::move(v1));
    EXPECT_EQ(ptr, v2.data());
    EXPECT_EQ(v2.size(), 10);
    for (auto i = 0; i < 10; ++i)
        EXPECT_EQ(v2[i], 11);
}

TEST_F(VecteurDynamicTests, ConstructFromArray) {
    std::array<int, 3> arr{10, 11, 12};
    Vecteur<int, std::dynamic_extent> v(arr);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicTests, ConstructFromOtherArray) {
    std::array<int, 3> arr{10, 11, 12};
    Vecteur<float, std::dynamic_extent> v(arr);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_FLOAT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicTests, ConstructFromSpan) {
    std::array<int, 3> arr{10, 11, 12};
    std::span<int, 3> span(arr);
    Vecteur<int, std::dynamic_extent> v(span);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicTests, ConstructFromOtherSpan) {
    std::array<int, 3> arr{10, 11, 12};
    std::span<int, 3> span(arr);
    Vecteur<float, std::dynamic_extent> v(span);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_FLOAT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicTests, ConstructFromDynamicSpan) {
    std::array<int, 3> arr{10, 11, 12};
    std::span<int, std::dynamic_extent> span(arr);
    Vecteur<int, std::dynamic_extent> v(span);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicTests, ConstructFromOtherDynamicSpan) {
    std::array<int, 3> arr{10, 11, 12};
    std::span<int, std::dynamic_extent> span(arr);
    Vecteur<float, std::dynamic_extent> v(span);
    EXPECT_EQ(v.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_FLOAT_EQ(v[i], arr[i]);
}

TEST_F(VecteurDynamicTests, ConstructFromStatic) {
    Vecteur<int, 3> v1(10, 11, 12);
    Vecteur<int, std::dynamic_extent> v2(v1);
    EXPECT_EQ(v2.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v2[i], v1[i]);
}

TEST_F(VecteurDynamicTests, ConstructFromOtherStatic) {
    Vecteur<int, 3> v1(10, 11, 12);
    Vecteur<float, std::dynamic_extent> v2(v1);

    EXPECT_EQ(v2.size(), 3);
    EXPECT_FLOAT_EQ(10, v2[0]);
    EXPECT_FLOAT_EQ(11, v2[1]);
    EXPECT_FLOAT_EQ(12, v2[2]);
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
    Vecteur<int, 3> v1(10, 11, 12);
    Vecteur<int, std::dynamic_extent> v2;
    auto *ptr = v2.data();
    v2 = v1;
    auto *ptr2 = v2.data();
    EXPECT_NE(ptr, ptr2);
    EXPECT_EQ(v2.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v2[i], v1[i]);
}

TEST_F(VecteurDynamicTests, AssignmentFromOtherStatic) {
    Vecteur<int, 3> v1(10, 11, 12);
    Vecteur<float, std::dynamic_extent> v2;
    v2 = v1;
    EXPECT_EQ(v2.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_FLOAT_EQ(v2[i], v1[i]);
}

TEST_F(VecteurDynamicTests, AssignmentFromDynamicSameSize) {
    Vecteur<int, std::dynamic_extent> v1{10, 11, 12};
    Vecteur<int, std::dynamic_extent> v2{13, 14, 15};
    auto *ptr = v1.data();
    v1 = v2;
    auto *ptr2 = v1.data();
    EXPECT_EQ(ptr, ptr2);
    EXPECT_EQ(v1.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v1[i], v2[i]);
}

TEST_F(VecteurDynamicTests, AssignmentFromOtherDynamicSameSize) {
    Vecteur<int, std::dynamic_extent> v1{10, 11, 12};
    Vecteur<float, std::dynamic_extent> v2{10, 11, 12};
    v2 = v1;

    EXPECT_EQ(v2.size(), 3);
    EXPECT_FLOAT_EQ(v2[0], 10);
    EXPECT_FLOAT_EQ(v2[1], 11);
    EXPECT_FLOAT_EQ(v2[2], 12);
}

TEST_F(VecteurDynamicTests, AssignemtFromDynamicLessSize) {
    Vecteur<int, std::dynamic_extent> v1{10, 11, 12};
    Vecteur<int, std::dynamic_extent> v2{13, 14};
    auto *ptr = v1.data();
    v1 = v2;
    auto *ptr2 = v1.data();
    EXPECT_NE(ptr, ptr2);
    EXPECT_EQ(v1.size(), 2);
    for (auto i = 0; i < 2; ++i)
        EXPECT_EQ(v1[i], v2[i]);
}

TEST_F(VecteurDynamicTests, AssignmentFromOtherDynamicLessSize) {
    Vecteur<int, std::dynamic_extent> v1{10, 11, 12};
    Vecteur<float, std::dynamic_extent> v2{13, 14};
    v2 = v1;

    EXPECT_EQ(v2.size(), 3);
    EXPECT_FLOAT_EQ(v2[0], 10);
    EXPECT_FLOAT_EQ(v2[1], 11);
    EXPECT_FLOAT_EQ(v2[2], 12);
}

TEST_F(VecteurDynamicTests, AssignmentFromDynamicMoreSize) {
    Vecteur<int, std::dynamic_extent> v1{10, 11};
    Vecteur<int, std::dynamic_extent> v2{13, 14, 15};
    auto *ptr = v1.data();
    v1 = v2;
    auto *ptr2 = v1.data();
    EXPECT_NE(ptr, ptr2);
    EXPECT_EQ(v1.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v1[i], v2[i]);
}

TEST_F(VecteurDynamicTests, AssignmentFromOtherDynamicMoreSize) {
    Vecteur<int, std::dynamic_extent> v1{10, 11};
    Vecteur<float, std::dynamic_extent> v2{13, 14, 15};
    v2 = v1;

    EXPECT_EQ(v2.size(), 2);
    EXPECT_FLOAT_EQ(v2[0], 10);
    EXPECT_FLOAT_EQ(v2[1], 11);
}

TEST_F(VecteurDynamicTests, AssignmentFromSelf) {
    Vecteur<int, std::dynamic_extent> v1{10, 11, 12};
    auto *ptr = v1.data();
    v1 = v1;
    auto *ptr2 = v1.data();
    EXPECT_EQ(ptr, ptr2);
    EXPECT_EQ(v1.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v1[i], 10 + i);
}

TEST_F(VecteurDynamicTests, MoveAssignmentFromSelf) {
    Vecteur<int, std::dynamic_extent> v1{10, 11, 12};
    auto *ptr = v1.data();
    v1 = std::move(v1);
    auto *ptr2 = v1.data();
    EXPECT_EQ(ptr, ptr2);
    EXPECT_EQ(v1.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v1[i], 10 + i);
}

TEST_F(VecteurDynamicTests, MoveAssignmentFromDynamic) {
    Vecteur<int, std::dynamic_extent> v1{10, 11, 12};
    Vecteur<int, std::dynamic_extent> v2{13, 14, 15};
    auto *ptr = v2.data();
    v1 = std::move(v2);
    auto *ptr2 = v1.data();
    EXPECT_EQ(ptr, ptr2);
    EXPECT_EQ(v1.size(), 3);
    for (auto i = 0; i < 3; ++i)
        EXPECT_EQ(v1[i], 13 + i);
}

TEST_F(VecteurDynamicTests, MoveAssignmentFromDynamicDifferentSize) {
    Vecteur<int, std::dynamic_extent> v1{10, 11, 12};
    Vecteur<int, std::dynamic_extent> v2{13, 14};
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
        !std::is_constructible_v<Vecteur<int, 3>, Vecteur<int, std::dynamic_extent>>,
        "Static vecteur should not be constructed from dynamic vecteur."
    );
}

TEST_F(VecteurDynamicTests, StaticEqStatic) {
    ASSERT_EQ(svIn1, svIn1);
    ASSERT_TRUE(svIn1 == svIn1);
}

TEST_F(VecteurDynamicTests, StaticEqDynamic) {
    ASSERT_EQ(svIn1, dvIn1);
    ASSERT_TRUE(svIn1 == dvIn1);
}

TEST_F(VecteurDynamicTests, DynamicEqStatic) {
    ASSERT_EQ(dvIn1, svIn1);
    ASSERT_TRUE(dvIn1 == svIn1);
}

#define VECTEUR_OP_TEST(op, name)                                                                  \
    TEST_F(VecteurDynamicTests, StaticDynamic##name) {                                             \
        ASSERT_EQ(svIn1.size(), dvIn1.size());                                                     \
        ASSERT_EQ(svIn1.size(), dvIn2.size());                                                     \
        svOut = svIn1 op svIn2;                                                                    \
        dvOut = svIn1 op dvIn2;                                                                    \
        EXPECT_TRUE(svOut == dvOut);                                                               \
    }                                                                                              \
    TEST_F(VecteurDynamicTests, DynamicStatic##name) {                                             \
        ASSERT_EQ(dvIn1.size(), svIn1.size());                                                     \
        ASSERT_EQ(dvIn1.size(), svIn2.size());                                                     \
        svOut = svIn1 op svIn2;                                                                    \
        dvOut = dvIn1 op svIn2;                                                                    \
        EXPECT_TRUE(svOut == dvOut);                                                               \
    }                                                                                              \
    TEST_F(VecteurDynamicTests, DynamicDynamic##name) {                                            \
        ASSERT_EQ(dvIn1.size(), dvIn2.size());                                                     \
        svOut = svIn1 op svIn2;                                                                    \
        dvOut = dvIn1 op dvIn2;                                                                    \
        EXPECT_TRUE(svOut == dvOut);                                                               \
    }

VECTEUR_OP_TEST(+, Add)
VECTEUR_OP_TEST(-, Sub)
VECTEUR_OP_TEST(*, Mult)
VECTEUR_OP_TEST(/, Div)

// no compilation error
#define VECTEUR_OP_TEST_FAIL(op, name)                                                             \
    TEST_F(VecteurDynamicTests, DynamicStaticFail##name) {                                         \
        EXPECT_DEATH_IF_SUPPORTED(dvOut = dvIn1 op svIn3, "");                                     \
    }                                                                                              \
    TEST_F(VecteurDynamicTests, StaticDynamicFail##name) {                                         \
        EXPECT_DEATH_IF_SUPPORTED(dvOut = svIn3 op dvIn1, "");                                     \
    }

VECTEUR_OP_TEST_FAIL(+, Add)
VECTEUR_OP_TEST_FAIL(-, Sub)
VECTEUR_OP_TEST_FAIL(*, Mult)
VECTEUR_OP_TEST_FAIL(/, Div)
