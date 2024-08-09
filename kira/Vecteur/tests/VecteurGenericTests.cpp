#include <gtest/gtest.h>

#include "kira/VecteurGeneric.h"
#include "kira/VecteurRouter.h"

using namespace kira;

class VecteurTests : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(VecteurTests, ZeroConstructor) {
    Vecteur<int, 3> x{0};
    EXPECT_EQ(0, x[0]);
    EXPECT_EQ(0, x[1]);
    EXPECT_EQ(0, x[2]);
}

TEST_F(VecteurTests, OneConstructor) {
    Vecteur<int, 3> x{1};
    EXPECT_EQ(1, x[0]);
    EXPECT_EQ(1, x[1]);
    EXPECT_EQ(1, x[2]);
}

TEST_F(VecteurTests, ArbitraryConstructor) {
    Vecteur<int, 3> x{1, 2, 3};
    EXPECT_EQ(1, x[0]);
    EXPECT_EQ(2, x[1]);
    EXPECT_EQ(3, x[2]);
}

TEST_F(VecteurTests, ConstructorWithSpan) {
    std::array<float, 3> arr{1.1f, 2.2f, 3.3f};
    std::span<float const, 3> sp(arr);
    Vecteur<float, 3> x(sp);
    EXPECT_FLOAT_EQ(1.1f, x[0]);
    EXPECT_FLOAT_EQ(2.2f, x[1]);
    EXPECT_FLOAT_EQ(3.3f, x[2]);
}

TEST_F(VecteurTests, ConstructorWithArray) {
    std::array<int, 4> arr = {5, 6, 7, 8};
    Vecteur<int, 4> x(arr);
    EXPECT_EQ(5, x[0]);
    EXPECT_EQ(6, x[1]);
    EXPECT_EQ(7, x[2]);
    EXPECT_EQ(8, x[3]);
}

template <typename T, size_t N, typename... Args>
concept ValidVecteurConstructor = requires { Vecteur<T, N>{std::declval<Args>()...}; };

TEST_F(VecteurTests, InvalidConstructor) {
    static_assert(
        !ValidVecteurConstructor<int, 3, int, int>,
        "Vecteur constructor with fewer elements than the size should not be valid"
    );

    static_assert(
        !ValidVecteurConstructor<int, 3, int, int, int, int>,
        "Vecteur constructor with more elements than the size should not be valid"
    );
}

TEST_F(VecteurTests, XYZAccessors) {
    Vecteur<int, 3> x{1, 2, 3};
    EXPECT_EQ(1, x.x());
    EXPECT_EQ(2, x.y());
    EXPECT_EQ(3, x.z());
}

TEST_F(VecteurTests, Addition) {
    Vecteur<int, 3> x{1, 2, 3};
    Vecteur<int, 3> y{3, 2, 1};
    auto z = x + y;

    EXPECT_EQ(4, z[0]);
    EXPECT_EQ(4, z[1]);
    EXPECT_EQ(4, z[2]);

    auto w1 = x + 1;
    EXPECT_EQ(3, w1.size());
    EXPECT_EQ(2, w1.x());
    EXPECT_EQ(3, w1.y());
    EXPECT_EQ(4, w1.z());

    auto w2 = 1 + x;
    EXPECT_EQ(3, w2.size());
    EXPECT_EQ(2, w2.x());
    EXPECT_EQ(3, w2.y());
    EXPECT_EQ(4, w2.z());
}

TEST_F(VecteurTests, Multiplication) {
    Vecteur<int, 3> x{1, 2, 3};
    Vecteur<int, 3> y{2, 3, 4};

    auto z = x * y;
    EXPECT_EQ(2, z[0]);
    EXPECT_EQ(6, z[1]);
    EXPECT_EQ(12, z[2]);

    auto w1 = 2 * x;
    EXPECT_EQ(3, w1.size());
    EXPECT_EQ(2, w1.x());
    EXPECT_EQ(4, w1.y());
    EXPECT_EQ(6, w1.z());

    auto w2 = x * 3;
    EXPECT_EQ(3, w2.size());
    EXPECT_EQ(3, w2.x());
    EXPECT_EQ(6, w2.y());
    EXPECT_EQ(9, w2.z());
}

TEST_F(VecteurTests, MultiplicationFloatingPoint) {
    Vecteur<double, 3> x{1.1, 2.2, 3.3};
    Vecteur<double, 3> y{2.5, 3.5, 4.5};

    auto z = x * y;
    EXPECT_NEAR(2.75, z[0], 0.01);
    EXPECT_NEAR(7.70, z[1], 0.01);
    EXPECT_NEAR(14.85, z[2], 0.01);

    auto w1 = 2.5 * x;
    EXPECT_EQ(3, w1.size());
    EXPECT_NEAR(2.75, w1.x(), 0.01);
    EXPECT_NEAR(5.50, w1.y(), 0.01);
    EXPECT_NEAR(8.25, w1.z(), 0.01);

    auto w2 = x * 3.5;
    EXPECT_EQ(3, w2.size());
    EXPECT_NEAR(3.85, w2.x(), 0.01);
    EXPECT_NEAR(7.70, w2.y(), 0.01);
    EXPECT_NEAR(11.55, w2.z(), 0.01);
}

TEST_F(VecteurTests, Division) {
    Vecteur<int, 3> x{6, 12, 18};
    Vecteur<int, 3> y{2, 3, 4};

    auto z = x / y;
    EXPECT_EQ(3, z[0]);
    EXPECT_EQ(4, z[1]);
    EXPECT_EQ(4, z[2]);

    auto w1 = x / 2;
    EXPECT_EQ(3, w1.size());
    EXPECT_EQ(3, w1.x());
    EXPECT_EQ(6, w1.y());
    EXPECT_EQ(9, w1.z());

    auto w2 = 36 / x;
    EXPECT_EQ(3, w2.size());
    EXPECT_EQ(6, w2.x());
    EXPECT_EQ(3, w2.y());
    EXPECT_EQ(2, w2.z());
}

TEST_F(VecteurTests, DivisionFloatingPoint) {
    Vecteur<double, 3> x{6.0, 12.0, 18.0};
    Vecteur<double, 3> y{2.0, 3.0, 4.0};

    auto z = x / y;
    EXPECT_NEAR(3.0, z[0], 0.01);
    EXPECT_NEAR(4.0, z[1], 0.01);
    EXPECT_NEAR(4.5, z[2], 0.01);

    auto w1 = x / 2.0;
    EXPECT_EQ(3, w1.size());
    EXPECT_NEAR(3.0, w1.x(), 0.01);
    EXPECT_NEAR(6.0, w1.y(), 0.01);
    EXPECT_NEAR(9.0, w1.z(), 0.01);

    auto w2 = 36.0 / x;
    EXPECT_EQ(3, w2.size());
    EXPECT_NEAR(6.0, w2.x(), 0.01);
    EXPECT_NEAR(3.0, w2.y(), 0.01);
    EXPECT_NEAR(2.0, w2.z(), 0.01);
}

TEST_F(VecteurTests, Max) {
    Vecteur<int, 3> x{1, 2, 3};
    Vecteur<int, 3> y{6, 5, 2};

    auto z = x.max(y);
    EXPECT_EQ(6, z[0]);
    EXPECT_EQ(5, z[1]);
    EXPECT_EQ(3, z[2]);
}

TEST_F(VecteurTests, MaxScalar) {
    Vecteur<int, 3> x{1, 2, 3};

    auto z = x.max(2);
    EXPECT_EQ(2, z[0]);
    EXPECT_EQ(2, z[1]);
    EXPECT_EQ(3, z[2]);
}

TEST_F(VecteurTests, Min) {
    Vecteur<int, 3> x{1, 2, 3};
    Vecteur<int, 3> y{6, 5, 2};

    auto z = x.min(y);
    EXPECT_EQ(1, z[0]);
    EXPECT_EQ(2, z[1]);
    EXPECT_EQ(2, z[2]);
}

TEST_F(VecteurTests, MinScalar) {
    Vecteur<int, 3> x{1, 2, 3};

    auto z = x.min(2);
    EXPECT_EQ(1, z[0]);
    EXPECT_EQ(2, z[1]);
    EXPECT_EQ(2, z[2]);
}

TEST_F(VecteurTests, MaxFloat) {
    Vecteur<double, 3> x{1.1, 2.2, 3.3};
    Vecteur<double, 3> y{6.6, 5.5, 2.2};

    auto z = x.max(y);
    EXPECT_NEAR(6.6, z[0], 1e-6);
    EXPECT_NEAR(5.5, z[1], 1e-6);
    EXPECT_NEAR(3.3, z[2], 1e-6);
}

TEST_F(VecteurTests, MaxScalarFloat) {
    Vecteur<double, 3> x{1.1, 2.2, 3.3};

    auto z = x.max(2.5);
    EXPECT_NEAR(2.5, z[0], 1e-6);
    EXPECT_NEAR(2.5, z[1], 1e-6);
    EXPECT_NEAR(3.3, z[2], 1e-6);
}

TEST_F(VecteurTests, MinFloat) {
    Vecteur<double, 3> x{1.1, 2.2, 3.3};
    Vecteur<double, 3> y{6.6, 5.5, 2.2};

    auto z = x.min(y);
    EXPECT_NEAR(1.1, z[0], 1e-6);
    EXPECT_NEAR(2.2, z[1], 1e-6);
    EXPECT_NEAR(2.2, z[2], 1e-6);
}

TEST_F(VecteurTests, MinScalarFloat) {
    Vecteur<double, 3> x{1.1, 2.2, 3.3};

    auto z = x.min(2.5);
    EXPECT_NEAR(1.1, z[0], 1e-6);
    EXPECT_NEAR(2.2, z[1], 1e-6);
    EXPECT_NEAR(2.5, z[2], 1e-6);
}

TEST_F(VecteurTests, DotProduct) {
    Vecteur<int, 3> ix{1, 2, 3};
    Vecteur<int, 3> iy{3, 2, 1};

    auto const z = ix.dot(iy);
    EXPECT_EQ(10, z);
}

TEST_F(VecteurTests, DotProductFloatingPoint) {
    Vecteur<float, 3> fx{1.5f, 2.5f, 3.5f};
    Vecteur<float, 3> fy{3.0f, 2.0f, 1.0f};

    auto const z = fx.dot(fy);
    EXPECT_FLOAT_EQ(13.0f, z);
}

TEST_F(VecteurTests, DotProductReturnTypeInt) {
    Vecteur<int, 3> ix{1, 2, 3};
    Vecteur<int, 3> iy{4, 5, 6};

    auto const z = ix.dot(iy);
    static_assert(
        std::is_same<decltype(z), int const>::value, "Dot product of int vectors should return int"
    );
    EXPECT_EQ(32, z);
}

TEST_F(VecteurTests, DotProductIntFloatingPoint) {
    Vecteur<int, 3> ix{1, 2, 3};
    Vecteur<float, 3> fy{3.0f, 2.0f, 1.0f};

    auto const z = ix.dot(fy);
    static_assert(
        std::is_same<decltype(z), float const>::value,
        "Dot product of int and float vectors should return float"
    );
    EXPECT_FLOAT_EQ(10.0f, z);
}

TEST_F(VecteurTests, DotProductFloatingPointInt) {
    Vecteur<float, 3> fx{1.5f, 2.5f, 3.5f};
    Vecteur<int, 3> iy{3, 2, 1};

    auto const z = fx.dot(iy);
    static_assert(
        std::is_same<decltype(z), float const>::value,
        "Dot product of float and int vectors should return float"
    );
    EXPECT_FLOAT_EQ(13.0f, z);
}

TEST_F(VecteurTests, DotProductZeroVector) {
    Vecteur<int, 3> ix{1, 2, 3};
    Vecteur<int, 3> zero{0, 0, 0};

    auto const z = ix.dot(zero);
    EXPECT_EQ(0, z);
}

TEST_F(VecteurTests, Eq) {
    Vecteur<int, 3> x{1, 2, 3};
    Vecteur<int, 3> y{1, 2, 3};

    EXPECT_TRUE(x.eq(y));
    EXPECT_TRUE(x == y);
    EXPECT_FALSE(x != y);
}

TEST_F(VecteurTests, EqFloatingPoint) {
    Vecteur<float, 3> x{1.0f, 2.0f, 3.0f};
    Vecteur<float, 3> y{1.0f, 2.0f, 3.0f};

    EXPECT_TRUE(x.eq(y));
    EXPECT_TRUE(x == y);
    EXPECT_FALSE(x != y);
}

TEST_F(VecteurTests, EqDifferent) {
    Vecteur<int, 3> x{1, 2, 3};
    Vecteur<int, 3> y{3, 2, 1};

    EXPECT_FALSE(x.eq(y));
    EXPECT_FALSE(x == y);
    EXPECT_TRUE(x != y);
}

TEST_F(VecteurTests, EqDifferentFloatingPoint) {
    Vecteur<float, 3> x{1.0f, 2.0f, 3.0f};
    Vecteur<float, 3> y{3.0f, 2.0f, 1.0f};

    EXPECT_FALSE(x.eq(y));
    EXPECT_FALSE(x == y);
    EXPECT_TRUE(x != y);
}

TEST_F(VecteurTests, Near) {
    float const epsilon = 1e-4F;

    Vecteur<float, 3> v1{1.0f, 2.0f, 3.0f};
    Vecteur<float, 3> v2{1.0f, 2.0f, 3.0f};
    EXPECT_TRUE(v1.near(v2, epsilon));

    Vecteur<float, 3> v3{1.0f, 2.0f, 3.0f + epsilon * 0.9f};
    EXPECT_TRUE(v1.near(v3, epsilon));

    Vecteur<float, 3> v4{1.0f, 2.0f, 3.0f + epsilon};
    EXPECT_TRUE(v1.near(v4, epsilon));

    Vecteur<float, 3> v5{1.0f, 2.0f, 3.0f + epsilon * 1.1f};
    EXPECT_FALSE(v1.near(v5, epsilon));

    Vecteur<float, 3> v6{1.0f + epsilon * 0.3f, 2.0f + epsilon * 0.3f, 3.0f + epsilon * 0.3f};
    EXPECT_TRUE(v1.near(v6, epsilon));

    Vecteur<float, 3> v7{1.0f + epsilon * 0.7f, 2.0f + epsilon * 0.7f, 3.0f + epsilon * 0.7f};
    EXPECT_FALSE(v1.near(v7, epsilon));

    Vecteur<float, 3> v8{-1.0f, -2.0f, -3.0f};
    Vecteur<float, 3> v9{-1.0f, -2.0f, -3.0f - epsilon * 0.9f};
    EXPECT_TRUE(v8.near(v9, epsilon));

    Vecteur<float, 3> v10{-1.0f, 2.0f, -3.0f};
    Vecteur<float, 3> v11{-1.0f + epsilon * 0.5f, 2.0f - epsilon * 0.5f, -3.0f + epsilon * 0.5f};
    EXPECT_TRUE(v10.near(v11, epsilon));

    Vecteur<float, 3> v12{0.0f, 0.0f, 0.0f};
    Vecteur<float, 3> v13{epsilon * 0.3f, epsilon * 0.3f, epsilon * 0.3f};
    EXPECT_TRUE(v12.near(v13, epsilon));

    Vecteur<float, 3> v14{1e-8f, 1e-8f, 1e-8f};
    Vecteur<float, 3> v15{1e-8f + epsilon * 0.5f, 1e-8f + epsilon * 0.5f, 1e-8f + epsilon * 0.5f};
    EXPECT_TRUE(v14.near(v15, epsilon));
}

TEST_F(VecteurTests, Hsum) {
    Vecteur<int, 3> v1{1, 2, 3};
    EXPECT_EQ(v1.hsum(), 6);

    Vecteur<float, 4> v2{1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_FLOAT_EQ(v2.hsum(), 10.0f);

    Vecteur<double, 2> v3{-1.5, 2.5};
    EXPECT_DOUBLE_EQ(v3.hsum(), 1.0);

    Vecteur<int, 5> v4{0, 0, 0, 0, 0};
    EXPECT_EQ(v4.hsum(), 0);

    Vecteur<float, 3> v5{1e-7f, 1e7f, 1.0f};
    EXPECT_FLOAT_EQ(v5.hsum(), 1e7f + 1.0f);
}

TEST_F(VecteurTests, Hprod) {
    Vecteur<int, 3> v1{1, 2, 3};
    EXPECT_EQ(v1.hprod(), 6);

    Vecteur<float, 4> v2{1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_FLOAT_EQ(v2.hprod(), 24.0f);

    Vecteur<double, 2> v3{-1.5, 2.5};
    EXPECT_DOUBLE_EQ(v3.hprod(), -3.75);

    Vecteur<int, 5> v4{1, 1, 1, 1, 1};
    EXPECT_EQ(v4.hprod(), 1);

    Vecteur<float, 3> v5{1e-7f, 1e7f, 2.0f};
    EXPECT_FLOAT_EQ(v5.hprod(), 2.0f);
}

TEST_F(VecteurTests, Hmax) {
    Vecteur<int, 3> v1{1, 3, 2};
    EXPECT_EQ(v1.hmax(), 3);

    Vecteur<float, 4> v2{1.0f, 4.0f, 3.0f, 2.0f};
    EXPECT_FLOAT_EQ(v2.hmax(), 4.0f);

    Vecteur<double, 2> v3{-1.5, 2.5};
    EXPECT_DOUBLE_EQ(v3.hmax(), 2.5);

    Vecteur<int, 5> v4{-1, -2, -3, -4, -5};
    EXPECT_EQ(v4.hmax(), -1);

    Vecteur<float, 3> v5{1e-7f, 1e7f, 1.0f};
    EXPECT_FLOAT_EQ(v5.hmax(), 1e7f);
}

TEST_F(VecteurTests, Hmin) {
    Vecteur<int, 3> v1{1, 3, 2};
    EXPECT_EQ(v1.hmin(), 1);

    Vecteur<float, 4> v2{1.0f, 4.0f, 3.0f, 2.0f};
    EXPECT_FLOAT_EQ(v2.hmin(), 1.0f);

    Vecteur<double, 2> v3{-1.5, 2.5};
    EXPECT_DOUBLE_EQ(v3.hmin(), -1.5);

    Vecteur<int, 5> v4{-1, -2, -3, -4, -5};
    EXPECT_EQ(v4.hmin(), -5);

    Vecteur<float, 3> v5{1e-7f, 1e7f, 1.0f};
    EXPECT_FLOAT_EQ(v5.hmin(), 1e-7f);
}

TEST_F(VecteurTests, HsumHprodWithZero) {
    Vecteur<int, 3> v1{0, 1, 2};
    EXPECT_EQ(v1.hsum(), 3);
    EXPECT_EQ(v1.hprod(), 0);

    Vecteur<float, 4> v2{0.0f, 1.0f, 2.0f, 3.0f};
    EXPECT_FLOAT_EQ(v2.hsum(), 6.0f);
    EXPECT_FLOAT_EQ(v2.hprod(), 0.0f);
}

TEST_F(VecteurTests, HmaxHminWithEqualElements) {
    Vecteur<int, 3> v1{2, 2, 2};
    EXPECT_EQ(v1.hmax(), 2);
    EXPECT_EQ(v1.hmin(), 2);

    Vecteur<float, 4> v2{1.5f, 1.5f, 1.5f, 1.5f};
    EXPECT_FLOAT_EQ(v2.hmax(), 1.5f);
    EXPECT_FLOAT_EQ(v2.hmin(), 1.5f);
}

TEST_F(VecteurTests, HsumHprodWithNegativeValues) {
    Vecteur<int, 3> v1{-1, 2, -3};
    EXPECT_EQ(v1.hsum(), -2);
    EXPECT_EQ(v1.hprod(), 6);

    Vecteur<float, 4> v2{-1.0f, 2.0f, -3.0f, 4.0f};
    EXPECT_FLOAT_EQ(v2.hsum(), 2.0f);
    EXPECT_FLOAT_EQ(v2.hprod(), 24.0f);
}

TEST_F(VecteurTests, Negate) {
    Vecteur<int, 3> x{1, 2, 3};
    auto y = -x;

    EXPECT_EQ(-1, y[0]);
    EXPECT_EQ(-2, y[1]);
    EXPECT_EQ(-3, y[2]);
}

TEST_F(VecteurTests, NegateFloatingPoint) {
    Vecteur<float, 3> x{1.0f, 2.0f, 3.0f};
    auto y = -x;

    EXPECT_NEAR(-1.0f, y[0], 1e-4);
    EXPECT_NEAR(-2.0f, y[1], 1e-4);
    EXPECT_NEAR(-3.0f, y[2], 1e-4);
}

TEST_F(VecteurTests, Sqr) {
    Vecteur<int, 3> x{1, 2, 3};
    auto y = x.sqr();

    EXPECT_EQ(1, y[0]);
    EXPECT_EQ(4, y[1]);
    EXPECT_EQ(9, y[2]);
}

TEST_F(VecteurTests, SqrFloatingPoint) {
    Vecteur<float, 3> x{-1.0f, -2.0f, -3.0f};
    auto y = x.sqr();

    EXPECT_NEAR(1.0f, y[0], 1e-4);
    EXPECT_NEAR(4.0f, y[1], 1e-4);
    EXPECT_NEAR(9.0f, y[2], 1e-4);
}

TEST_F(VecteurTests, ToSpan) {
    Vecteur<int, 3> x{1, 2, 3};

    {
        auto span = x.to_span();
        EXPECT_EQ(3, span.size());
        EXPECT_EQ(1, span[0]);
        EXPECT_EQ(2, span[1]);
        EXPECT_EQ(3, span[2]);

        span[1] = 5;
        EXPECT_EQ(5, x[1]);
    }

    {
        auto const &constX = x;
        auto constSpan = constX.to_span();
        EXPECT_EQ(3, constSpan.size());
        EXPECT_EQ(1, constSpan[0]);
        EXPECT_EQ(5, constSpan[1]);
        EXPECT_EQ(3, constSpan[2]);
    }
}

TEST_F(VecteurTests, ToArray) {
    Vecteur<int, 3> x{1, 2, 3};

    auto arr = x.to_array();
    EXPECT_EQ(3, arr.size());
    EXPECT_EQ(1, arr[0]);
    EXPECT_EQ(2, arr[1]);
    EXPECT_EQ(3, arr[2]);

    arr[1] = 5;
    EXPECT_EQ(2, x[1]);
}

// fail to compile
#if 0
TEST_F(VecteurTests, DifferentSizeBinaryOperation) {
    Vecteur<int, 2> x{1, 2};
    Vecteur<int, 3> y{1, 2, 3};

    auto z = x + y;
}
#endif

#if 0
TEST_F(VecteurTests, DifferentSizeDotProduct) {
    Vecteur<int, 2> x{1, 2};
    Vecteur<int, 3> y{1, 2, 3};

    auto z = x.dot(y);
    EXPECT_EQ(5, z);

    Vecteur<int, std::dynamic_extent> dx{};
    Vecteur<int, 3> dy{};

    z = dx.dot(dy);
}
#endif
