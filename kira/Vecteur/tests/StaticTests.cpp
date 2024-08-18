#include <gtest/gtest.h>

#include <type_traits>

#include "ArithLib.h"
#include "VecteurTests.h"

using namespace kira;

static_assert(is_vecteur<Vecteur<float, 3, VecteurBackend::Generic>>);
static_assert(is_vecteur<std::add_lvalue_reference_t<Vecteur<float, 3, VecteurBackend::Generic>>>);

class VecteurStaticTests : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

template <typename T, size_t N, typename... Args>
concept ValidVecteurConstructor = requires {
    Vecteur<T, N, VecteurBackend::Generic>{std::declval<Args>()...};
    Vecteur<T, N, VecteurBackend::Lazy>{std::declval<Args>()...};
};

#if (defined(__clang__) and (__clang_major__ >= 18)) or (defined(__GNUC__) and (__GNUC__ >= 11))
TEST_F(VecteurStaticTests, InvalidConstructor) {
    static_assert(
        !ValidVecteurConstructor<int, 3, int, int>,
        "Vecteur constructor with fewer elements than the size should not be valid"
    );

    static_assert(ValidVecteurConstructor<int, 3, int, int, int>);
    static_assert(ValidVecteurConstructor<float, 3, int, int, int>);

    static_assert(
        !ValidVecteurConstructor<int, 3, int, int, int, int>,
        "Vecteur constructor with more elements than the size should not be valid"
    );

    // Indeed not possible.
    // Vecteur<int, 3, VecteurBackend::Generic> x{1.0, 2, 3};
}
#endif

TEST_F(VecteurStaticTests, ZeroConstructor) {
    InstantiateStaticTests<int, 3>([]<typename Vecteur>() {
        Vecteur v{0};
        EXPECT_EQ(0, v.x());
        EXPECT_EQ(0, v.y());
        EXPECT_EQ(0, v.z());
    });

    InstantiateStaticTests<float, 3>([]<typename Vecteur>() {
        Vecteur v{0};
        EXPECT_FLOAT_EQ(0, v.x());
        EXPECT_FLOAT_EQ(0, v.y());
        EXPECT_FLOAT_EQ(0, v.z());
    });

    InstantiateStaticTests<int, 4>([]<typename Vecteur>() {
        Vecteur v{0};
        EXPECT_EQ(0, v.x());
        EXPECT_EQ(0, v.y());
        EXPECT_EQ(0, v.z());
        EXPECT_EQ(0, v.w());
    });
}

TEST_F(VecteurStaticTests, OneConstructor) {
    InstantiateStaticTests<int, 3>([]<typename Vecteur>() {
        Vecteur v{1};
        EXPECT_EQ(1, v.x());
        EXPECT_EQ(1, v.y());
        EXPECT_EQ(1, v.z());
    });

    InstantiateStaticTests<float, 3>([]<typename Vecteur>() {
        Vecteur v{1};
        EXPECT_FLOAT_EQ(1, v.x());
        EXPECT_FLOAT_EQ(1, v.y());
        EXPECT_FLOAT_EQ(1, v.z());
    });

    InstantiateStaticTests<int, 4>([]<typename Vecteur>() {
        Vecteur v{1};
        EXPECT_EQ(1, v.x());
        EXPECT_EQ(1, v.y());
        EXPECT_EQ(1, v.z());
        EXPECT_EQ(1, v.w());
    });
}

TEST_F(VecteurStaticTests, ArbitraryConstructor) {
    InstantiateStaticTests<int, 3>([]<typename Vecteur>() {
        Vecteur v{1, 2, 3};
        EXPECT_EQ(1, v[0]);
        EXPECT_EQ(2, v[1]);
        EXPECT_EQ(3, v[2]);
    });

    InstantiateStaticTests<float, 3>([]<typename Vecteur>() {
        Vecteur v{1.1f, 2.2f, 3.3f};
        EXPECT_FLOAT_EQ(1.1f, v[0]);
        EXPECT_FLOAT_EQ(2.2f, v[1]);
        EXPECT_FLOAT_EQ(3.3f, v[2]);
    });

    InstantiateStaticTests<int, 4>([]<typename Vecteur>() {
        Vecteur v{1, 2, 3, 4};
        EXPECT_EQ(1, v[0]);
        EXPECT_EQ(2, v[1]);
        EXPECT_EQ(3, v[2]);
        EXPECT_EQ(4, v[3]);
    });
}

TEST_F(VecteurStaticTests, ConstructorWithSpan) {
    InstantiateStaticTests<float, 3>([]<typename Vecteur>() {
        std::array<float, 3> arr{1.1f, 2.2f, 3.3f};
        std::span<float const, 3> sp(arr);
        Vecteur v(sp);
        EXPECT_FLOAT_EQ(1.1f, v[0]);
        EXPECT_FLOAT_EQ(2.2f, v[1]);
        EXPECT_FLOAT_EQ(3.3f, v[2]);
    });

    InstantiateStaticTests<int, 3>([]<typename Vecteur>() {
        std::array<int, 3> arr{1, 2, 3};
        std::span<int const, 3> sp(arr);
        Vecteur v(sp);
        EXPECT_EQ(1, v[0]);
        EXPECT_EQ(2, v[1]);
        EXPECT_EQ(3, v[2]);
    });

    InstantiateStaticTests<int, 4>([]<typename Vecteur>() {
        std::array<int, 4> arr{1, 2, 3, 4};
        std::span<int const, 4> sp(arr);
        Vecteur v(sp);
        EXPECT_EQ(1, v[0]);
        EXPECT_EQ(2, v[1]);
        EXPECT_EQ(3, v[2]);
        EXPECT_EQ(4, v[3]);
    });
}

TEST_F(VecteurStaticTests, ConstructorWithArray) {
    InstantiateStaticTests<int, 4>([]<typename Vecteur>() {
        std::array<int, 4> arr = {5, 6, 7, 8};
        Vecteur v(arr);
        EXPECT_EQ(5, v[0]);
        EXPECT_EQ(6, v[1]);
        EXPECT_EQ(7, v[2]);
        EXPECT_EQ(8, v[3]);
    });

    InstantiateStaticTests<float, 3>([]<typename Vecteur>() {
        std::array<float, 3> arr = {1.1f, 2.2f, 3.3f};
        Vecteur v(arr);
        EXPECT_FLOAT_EQ(1.1f, v[0]);
        EXPECT_FLOAT_EQ(2.2f, v[1]);
        EXPECT_FLOAT_EQ(3.3f, v[2]);
    });

    InstantiateStaticTests<int, 3>([]<typename Vecteur>() {
        std::array<int, 3> arr = {1, 2, 3};
        Vecteur v(arr);
        EXPECT_EQ(1, v[0]);
        EXPECT_EQ(2, v[1]);
        EXPECT_EQ(3, v[2]);
    });
}

TEST_F(VecteurStaticTests, XYZAccessors) {
    InstantiateStaticTests<int, 3>([]<typename Vecteur> {
        Vecteur v{1, 2, 3};
        EXPECT_EQ(1, v.x());
        EXPECT_EQ(2, v.y());
        EXPECT_EQ(3, v.z());
    });

    InstantiateStaticTests<int, 3>([]<typename Vecteur> {
        Vecteur v{3, 2, 1};
        EXPECT_EQ(3, v.x());
        EXPECT_EQ(2, v.y());
        EXPECT_EQ(1, v.z());
    });

    InstantiateStaticTests<float, 3>([]<typename Vecteur> {
        Vecteur v{1.1F, 2.1F, 3.1F};
        EXPECT_FLOAT_EQ(1.1F, v.x());
        EXPECT_FLOAT_EQ(2.1F, v.y());
        EXPECT_FLOAT_EQ(3.1F, v.z());
    });

    InstantiateStaticTests<float, 3>([]<typename Vecteur> {
        Vecteur v{3.1F, 2.1F, 1.1F};
        EXPECT_FLOAT_EQ(3.1F, v.x());
        EXPECT_FLOAT_EQ(2.1F, v.y());
        EXPECT_FLOAT_EQ(1.1F, v.z());
    });
}

TEST_F(VecteurStaticTests, ConstexprEval) {
    constexpr Vecteur<int, 3, VecteurBackend::Generic> x{1, 2, 3};
    constexpr Vecteur<int, 3, VecteurBackend::Generic> y{2, 3, 4};
    constexpr auto z = (x + y).eval();

    static_assert(z[0] == 3);
    static_assert(z[1] == 5);
    static_assert(z[2] == 7);
}

TEST_F(VecteurStaticTests, Addition) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};
        Vecteur y{3, 2, 1};
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
    });

    InstantiateStaticTests<float, 3>([&]<typename Vecteur>() {
        Vecteur x{1.1f, 2.2f, 3.3f};
        Vecteur y{3.3f, 2.2f, 1.1f};
        auto z = x + y;

        EXPECT_FLOAT_EQ(4.4f, z[0]);
        EXPECT_FLOAT_EQ(4.4f, z[1]);
        EXPECT_FLOAT_EQ(4.4f, z[2]);

        auto w1 = x + 1.0f;
        EXPECT_EQ(3, w1.size());
        EXPECT_FLOAT_EQ(2.1f, w1.x());
        EXPECT_FLOAT_EQ(3.2f, w1.y());
        EXPECT_FLOAT_EQ(4.3f, w1.z());

        auto w2 = 1.0f + x;
        EXPECT_EQ(3, w2.size());
        EXPECT_FLOAT_EQ(2.1f, w2.x());
        EXPECT_FLOAT_EQ(3.2f, w2.y());
        EXPECT_FLOAT_EQ(4.3f, w2.z());
    });
}

TEST_F(VecteurStaticTests, Multiplication) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};
        Vecteur y{2, 3, 4};

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
    });

    InstantiateStaticTests<double, 3>([&]<typename Vecteur>() {
        Vecteur x{1.1, 2.2, 3.3};
        Vecteur y{2.5, 3.5, 4.5};

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
    });
}

TEST_F(VecteurStaticTests, Division) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{6, 12, 18};
        Vecteur y{2, 3, 4};

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
    });

    InstantiateStaticTests<double, 3>([&]<typename Vecteur>() {
        Vecteur x{6.0, 12.0, 18.0};
        Vecteur y{2.0, 3.0, 4.0};

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
    });
}

TEST_F(VecteurStaticTests, Normalize) {
    InstantiateStaticTests<float, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};
        auto y = x.normalize();

        EXPECT_FLOAT_EQ(1.0f / std::sqrt(14.0f), y[0]);
        EXPECT_FLOAT_EQ(2.0f / std::sqrt(14.0f), y[1]);
        EXPECT_FLOAT_EQ(3.0f / std::sqrt(14.0f), y[2]);
    });
}

TEST_F(VecteurStaticTests, Max) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};
        Vecteur y{6, 5, 2};

        auto z = x.max(y);
        EXPECT_EQ(6, z[0]);
        EXPECT_EQ(5, z[1]);
        EXPECT_EQ(3, z[2]);

        auto w = x.max(2);
        EXPECT_EQ(2, w[0]);
        EXPECT_EQ(2, w[1]);
        EXPECT_EQ(3, w[2]);
    });

    InstantiateStaticTests<double, 3>([&]<typename Vecteur>() {
        Vecteur x{1.1, 2.2, 3.3};
        Vecteur y{6.6, 5.5, 2.2};

        auto z = x.max(y);
        EXPECT_NEAR(6.6, z[0], 1e-6);
        EXPECT_NEAR(5.5, z[1], 1e-6);
        EXPECT_NEAR(3.3, z[2], 1e-6);

        auto w = x.max(2.5);
        EXPECT_NEAR(2.5, w[0], 1e-6);
        EXPECT_NEAR(2.5, w[1], 1e-6);
        EXPECT_NEAR(3.3, w[2], 1e-6);
    });
}

TEST_F(VecteurStaticTests, Min) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};
        Vecteur y{6, 5, 2};

        auto z = x.min(y);
        EXPECT_EQ(1, z[0]);
        EXPECT_EQ(2, z[1]);
        EXPECT_EQ(2, z[2]);

        auto w = x.min(2);
        EXPECT_EQ(1, w[0]);
        EXPECT_EQ(2, w[1]);
        EXPECT_EQ(2, w[2]);
    });

    InstantiateStaticTests<double, 3>([&]<typename Vecteur>() {
        Vecteur x{1.1, 2.2, 3.3};
        Vecteur y{6.6, 5.5, 2.2};

        auto z = x.min(y);
        EXPECT_NEAR(1.1, z[0], 1e-6);
        EXPECT_NEAR(2.2, z[1], 1e-6);
        EXPECT_NEAR(2.2, z[2], 1e-6);

        auto w = x.min(2.5);
        EXPECT_NEAR(1.1, w[0], 1e-6);
        EXPECT_NEAR(2.2, w[1], 1e-6);
        EXPECT_NEAR(2.5, w[2], 1e-6);
    });
}

TEST_F(VecteurStaticTests, DotProduct) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};
        Vecteur y{3, 2, 1};

        auto const z = x.dot(y);
        EXPECT_EQ(10, z);

        static_assert(
            std::is_same<decltype(z), int const>::value,
            "Dot product of int vectors should return int"
        );
    });

    InstantiateStaticTests<float, 3>([&]<typename Vecteur>() {
        Vecteur x{1.5f, 2.5f, 3.5f};
        Vecteur y{3.0f, 2.0f, 1.0f};

        auto const z = x.dot(y);
        EXPECT_FLOAT_EQ(13.0f, z);
    });

    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};
        Vecteur zero{0, 0, 0};

        auto const z = x.dot(zero);
        EXPECT_EQ(0, z);
    });

    {
        Vecteur<int, 3, VecteurBackend::Generic> x{1, 2, 3};
        Vecteur<float, 3, VecteurBackend::Generic> y{3.0f, 2.0f, 1.0f};

        auto const z = x.dot(y);
        EXPECT_FLOAT_EQ(10.0f, z);
        static_assert(
            std::is_same<decltype(z), float const>::value,
            "Dot product of int and float vectors should return float"
        );
    }
}

TEST_F(VecteurStaticTests, Eq) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};
        Vecteur y{1, 2, 3};
        Vecteur z{3, 2, 1};

        EXPECT_TRUE(x.eq(y));
        EXPECT_TRUE(x == y);
        EXPECT_FALSE(x != y);

        EXPECT_FALSE(x.eq(z));
        EXPECT_FALSE(x == z);
        EXPECT_TRUE(x != z);
    });

    InstantiateStaticTests<float, 3>([&]<typename Vecteur>() {
        Vecteur x{1.0f, 2.0f, 3.0f};
        Vecteur y{1.0f, 2.0f, 3.0f};
        Vecteur z{3.0f, 2.0f, 1.0f};

        EXPECT_TRUE(x.eq(y));
        EXPECT_TRUE(x == y);
        EXPECT_FALSE(x != y);

        EXPECT_FALSE(x.eq(z));
        EXPECT_FALSE(x == z);
        EXPECT_TRUE(x != z);
    });
}

TEST_F(VecteurStaticTests, Near) {
    InstantiateStaticTests<float, 3>([&]<typename Vecteur>() {
        float const epsilon = 1e-4F;

        Vecteur v1{1.0f, 2.0f, 3.0f};
        Vecteur v2{1.0f, 2.0f, 3.0f};
        EXPECT_TRUE(v1.near(v2, epsilon));

        Vecteur v3{1.0f, 2.0f, 3.0f + epsilon * 0.9f};
        EXPECT_TRUE(v1.near(v3, epsilon));

        Vecteur v4{1.0f, 2.0f, 3.0f + epsilon};
        EXPECT_TRUE(v1.near(v4, epsilon));

        Vecteur v5{1.0f, 2.0f, 3.0f + epsilon * 1.1f};
        EXPECT_FALSE(v1.near(v5, epsilon));

        Vecteur v6{1.0f + epsilon * 0.3f, 2.0f + epsilon * 0.3f, 3.0f + epsilon * 0.3f};
        EXPECT_TRUE(v1.near(v6, epsilon));

        Vecteur v7{1.0f + epsilon * 0.7f, 2.0f + epsilon * 0.7f, 3.0f + epsilon * 0.7f};
        EXPECT_FALSE(v1.near(v7, epsilon));

        Vecteur v8{-1.0f, -2.0f, -3.0f};
        Vecteur v9{-1.0f, -2.0f, -3.0f - epsilon * 0.9f};
        EXPECT_TRUE(v8.near(v9, epsilon));

        Vecteur v10{-1.0f, 2.0f, -3.0f};
        Vecteur v11{-1.0f + epsilon * 0.5f, 2.0f - epsilon * 0.5f, -3.0f + epsilon * 0.5f};
        EXPECT_TRUE(v10.near(v11, epsilon));

        Vecteur v12{0.0f, 0.0f, 0.0f};
        Vecteur v13{epsilon * 0.3f, epsilon * 0.3f, epsilon * 0.3f};
        EXPECT_TRUE(v12.near(v13, epsilon));

        Vecteur v14{1e-8f, 1e-8f, 1e-8f};
        Vecteur v15{1e-8f + epsilon * 0.5f, 1e-8f + epsilon * 0.5f, 1e-8f + epsilon * 0.5f};
        EXPECT_TRUE(v14.near(v15, epsilon));
    });
}

TEST_F(VecteurStaticTests, HorizontalOperations) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur v1{1, 2, 3};
        EXPECT_EQ(v1.norm2(), 14);
        EXPECT_FLOAT_EQ(v1.norm(), std::sqrt(14.0));
        EXPECT_EQ(v1.hsum(), 6);
        EXPECT_EQ(v1.hprod(), 6);
        EXPECT_EQ(v1.hmax(), 3);
        EXPECT_EQ(v1.hmin(), 1);

        Vecteur v2{0, 1, 2};
        EXPECT_EQ(v2.hsum(), 3);
        EXPECT_EQ(v2.hprod(), 0);

        Vecteur v3{2, 2, 2};
        EXPECT_EQ(v3.hmax(), 2);
        EXPECT_EQ(v3.hmin(), 2);

        Vecteur v4{-1, 2, -3};
        EXPECT_EQ(v4.hsum(), -2);
        EXPECT_EQ(v4.hprod(), 6);
    });

    InstantiateStaticTests<float, 4>([&]<typename Vecteur>() {
        Vecteur v1{1.0f, 2.0f, 3.0f, 4.0f};
        EXPECT_FLOAT_EQ(v1.norm2(), 30.0f);
        EXPECT_FLOAT_EQ(v1.norm(), std::sqrt(30.0f));
        EXPECT_FLOAT_EQ(v1.hsum(), 10.0f);
        EXPECT_FLOAT_EQ(v1.hprod(), 24.0f);
        EXPECT_FLOAT_EQ(v1.hmax(), 4.0f);
        EXPECT_FLOAT_EQ(v1.hmin(), 1.0f);

        Vecteur v2{0.0f, 1.0f, 2.0f, 3.0f};
        EXPECT_FLOAT_EQ(v2.hsum(), 6.0f);
        EXPECT_FLOAT_EQ(v2.hprod(), 0.0f);

        Vecteur v3{1.5f, 1.5f, 1.5f, 1.5f};
        EXPECT_FLOAT_EQ(v3.hmax(), 1.5f);
        EXPECT_FLOAT_EQ(v3.hmin(), 1.5f);

        Vecteur v4{-1.0f, 2.0f, -3.0f, 4.0f};
        EXPECT_FLOAT_EQ(v4.hsum(), 2.0f);
        EXPECT_FLOAT_EQ(v4.hprod(), 24.0f);
    });
}

TEST_F(VecteurStaticTests, Negate) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};
        auto y = -x;

        EXPECT_EQ(-1, y[0]);
        EXPECT_EQ(-2, y[1]);
        EXPECT_EQ(-3, y[2]);
    });

    InstantiateStaticTests<float, 3>([&]<typename Vecteur>() {
        Vecteur x{1.0f, 2.0f, 3.0f};
        auto y = -x;

        EXPECT_NEAR(-1.0f, y[0], 1e-4);
        EXPECT_NEAR(-2.0f, y[1], 1e-4);
        EXPECT_NEAR(-3.0f, y[2], 1e-4);
    });
}

TEST_F(VecteurStaticTests, Sqr) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};
        auto y = x.sqr();

        EXPECT_EQ(1, y[0]);
        EXPECT_EQ(4, y[1]);
        EXPECT_EQ(9, y[2]);
    });

    InstantiateStaticTests<float, 3>([&]<typename Vecteur>() {
        Vecteur x{-1.0f, -2.0f, -3.0f};
        auto y = x.sqr();

        EXPECT_NEAR(1.0f, y[0], 1e-4);
        EXPECT_NEAR(4.0f, y[1], 1e-4);
        EXPECT_NEAR(9.0f, y[2], 1e-4);
    });
}

TEST_F(VecteurStaticTests, ToSpan) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};

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
    });
}

TEST_F(VecteurStaticTests, ToArray) {
    InstantiateStaticTests<int, 3>([&]<typename Vecteur>() {
        Vecteur x{1, 2, 3};

        auto arr = x.to_array();
        EXPECT_EQ(3, arr.size());
        EXPECT_EQ(1, arr[0]);
        EXPECT_EQ(2, arr[1]);
        EXPECT_EQ(3, arr[2]);

        arr[1] = 5;
        EXPECT_EQ(2, x[1]);
    });
}

TEST_F(VecteurStaticTests, FresnelConductor) {
    Vecteur<float, 3, VecteurBackend::Lazy> etaI{1.0f, 1.1f, 1.2f};
    Vecteur<float, 3, VecteurBackend::Lazy> etaT{1.5f, 1.6f, 1.7f};
    Vecteur<float, 3, VecteurBackend::Lazy> k{0.3f, 0.4f, 0.5f};

    Vecteur<float, 3, VecteurBackend::Generic> etaI2{1.0f, 1.1f, 1.2f};
    Vecteur<float, 3, VecteurBackend::Generic> etaT2{1.5f, 1.6f, 1.7f};
    Vecteur<float, 3, VecteurBackend::Generic> k2{0.3f, 0.4f, 0.5f};

    auto resultLazy = FresnelConductor(0.5f, etaI, etaT, k);
    auto resultGeneric = FresnelConductor(0.5f, etaI2, etaT2, k2);
    auto resultLazyExpanded = FresnelConductorExpanded(0.5f, etaI, etaT, k);

    EXPECT_FLOAT_EQ(resultGeneric[0], resultLazy[0]);
    EXPECT_FLOAT_EQ(resultGeneric[1], resultLazy[1]);
    EXPECT_FLOAT_EQ(resultGeneric[2], resultLazy[2]);
    EXPECT_FLOAT_EQ(resultGeneric[0], resultLazyExpanded[0]);
    EXPECT_FLOAT_EQ(resultGeneric[1], resultLazyExpanded[1]);
    EXPECT_FLOAT_EQ(resultGeneric[2], resultLazyExpanded[2]);
}

// fail to compile
#if 0
TEST_F(VecteurStaticTests, DifferentSizeBinaryOperation) {
    Vecteur<int, 2> x{1, 2};
    Vecteur<int, 3> y{1, 2, 3};

    auto z = x + y;
}
#endif

#if 0
TEST_F(VecteurStaticTests, DifferentType) {
    Vecteur<float, 3> y{1, 2, 3};
    Vecteur<int, 3> x{y};

    auto z = x + y;
}
#endif

#if 0
TEST_F(VecteurStaticTests, DifferentSizeDotProduct) {
    Vecteur<int, 2> x{1, 2};
    Vecteur<int, 3> y{1, 2, 3};

    auto z = x.dot(y);
    EXPECT_EQ(5, z);

    Vecteur<int, std::dynamic_extent> dx{};
    Vecteur<int, 3> dy{};

    z = dx.dot(dy);
}
#endif
