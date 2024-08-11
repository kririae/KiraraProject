#include <gtest/gtest.h>

#include "kira/Logger.h"
#include "kira/Vecteur.h"

using namespace kira;

class VecteurLazyTests : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(VecteurLazyTests, ZeroConstructor) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{0};
    EXPECT_EQ(0, x[0]);
    EXPECT_EQ(0, x[1]);
    EXPECT_EQ(0, x[2]);
}

TEST_F(VecteurLazyTests, OneConstructor) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{1};
    EXPECT_EQ(1, x[0]);
    EXPECT_EQ(1, x[1]);
    EXPECT_EQ(1, x[2]);
}

TEST_F(VecteurLazyTests, ArbitraryConstructor) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{1, 2, 3};
    EXPECT_EQ(1, x[0]);
    EXPECT_EQ(2, x[1]);
    EXPECT_EQ(3, x[2]);
}

TEST_F(VecteurLazyTests, ConstructorWithSpan) {
    std::array<float, 3> arr{1.1f, 2.2f, 3.3f};
    std::span<float const, 3> sp(arr);
    Vecteur<float, 3, VecteurBackend::Lazy> x(sp);
    EXPECT_FLOAT_EQ(1.1f, x[0]);
    EXPECT_FLOAT_EQ(2.2f, x[1]);
    EXPECT_FLOAT_EQ(3.3f, x[2]);
}

TEST_F(VecteurLazyTests, ConstructorWithArray) {
    std::array<int, 4> arr = {5, 6, 7, 8};
    Vecteur<int, 4, VecteurBackend::Lazy> x(arr);
    EXPECT_EQ(5, x[0]);
    EXPECT_EQ(6, x[1]);
    EXPECT_EQ(7, x[2]);
    EXPECT_EQ(8, x[3]);
}

template <typename T, size_t N, typename... Args>
concept ValidVecteurConstructor =
    requires { Vecteur<T, N, VecteurBackend::Lazy>{std::declval<Args>()...}; };

TEST_F(VecteurLazyTests, InvalidConstructor) {
    static_assert(
        !ValidVecteurConstructor<int, 3, int, int>,
        "Vecteur constructor with fewer elements than the size should not be valid"
    );

    static_assert(
        !ValidVecteurConstructor<int, 3, int, int, int, int>,
        "Vecteur constructor with more elements than the size should not be valid"
    );
}

TEST_F(VecteurLazyTests, XYZAccessors) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{1, 2, 3};
    EXPECT_EQ(1, x.x());
    EXPECT_EQ(2, x.y());
    EXPECT_EQ(3, x.z());
}

TEST_F(VecteurLazyTests, Addition) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{1, 2, 3};
    Vecteur<int, 3, VecteurBackend::Lazy> y{3, 2, 1};
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

TEST_F(VecteurLazyTests, Multiplication) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{1, 2, 3};
    Vecteur<int, 3, VecteurBackend::Lazy> y{2, 3, 4};

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

TEST_F(VecteurLazyTests, MultiplicationFloatingPoint) {
    Vecteur<double, 3, VecteurBackend::Lazy> x{1.1, 2.2, 3.3};
    Vecteur<double, 3, VecteurBackend::Lazy> y{2.5, 3.5, 4.5};

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

TEST_F(VecteurLazyTests, Division) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{6, 12, 18};
    Vecteur<int, 3, VecteurBackend::Lazy> y{2, 3, 4};

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

TEST_F(VecteurLazyTests, DivisionFloatingPoint) {
    Vecteur<double, 3, VecteurBackend::Lazy> x{6.0, 12.0, 18.0};
    Vecteur<double, 3, VecteurBackend::Lazy> y{2.0, 3.0, 4.0};

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

namespace {
template <is_vecteur Spectrum>
inline auto
FresnelConductor(float cos_theta_i, Spectrum const &etaI, Spectrum const &etaT, Spectrum const &k) {
    cos_theta_i = std::clamp<float>(cos_theta_i, -1, 1);
    Spectrum const eta = etaT / etaI;
    Spectrum const etak = k / etaI;

    auto const c2 = cos_theta_i * cos_theta_i;
    auto const si = 1 - c2;
    auto const eta2 = eta * eta;
    auto const etak2 = etak * etak;
    auto const t0 = eta2 - etak2 - si;
    auto const apb = (t0 * t0 + eta2 * etak2 * 4).sqrt();
    auto const t1 = apb + c2;
    auto const &a = ((apb + t0) * 0.5_R).sqrt();
    auto const t2 = a * cos_theta_i * 2;
    auto const rs = (t1 - t2) / (t1 + t2);
    auto const t3 = si * si + apb * c2;
    auto const t4 = t2 * si;
    auto const rp = rs * (t3 - t4) / (t3 + t4);

    // Be cautious with the auto return type deduction here.
    return ((rp + rs) * 0.5_R).eval();
}

template <is_vecteur Spectrum>
inline auto FresnelConductorExpanded(
    float cos_theta_i, Spectrum const &etaI, Spectrum const &etaT, Spectrum const &k
) {
    cos_theta_i = std::clamp<float>(cos_theta_i, -1, 1);
    Spectrum const eta = etaT / etaI;
    Spectrum const etak = k / etaI;

    auto const c2 = cos_theta_i * cos_theta_i;
    auto const si = 1 - c2;
    Spectrum const eta2 = eta * eta;
    Spectrum const etak2 = etak * etak;
    Spectrum const t0 = eta2 - etak2 - si;
    Spectrum const apb = (t0 * t0 + eta2 * etak2 * 4).sqrt();
    Spectrum const t1 = apb + c2;
    Spectrum const a = ((apb + t0) * 0.5_R).sqrt();
    Spectrum const t2 = a * cos_theta_i * 2;
    Spectrum const rs = (t1 - t2) / (t1 + t2);
    Spectrum const t3 = si * si + apb * c2;
    Spectrum const t4 = t2 * si;
    Spectrum const rp = rs * (t3 - t4) / (t3 + t4);

    // Be cautious with the auto return type deduction here.
    return ((rp + rs) * 0.5_R).eval();
}
} // namespace

TEST_F(VecteurLazyTests, FresnelConductor) {
    Vecteur<float, 3, VecteurBackend::Lazy> etaI{1.0f, 1.1f, 1.2f};
    Vecteur<float, 3, VecteurBackend::Lazy> etaT{1.5f, 1.6f, 1.7f};
    Vecteur<float, 3, VecteurBackend::Lazy> k{0.3f, 0.4f, 0.5f};

    Vecteur<float, 3> etaI2{1.0f, 1.1f, 1.2f};
    Vecteur<float, 3> etaT2{1.5f, 1.6f, 1.7f};
    Vecteur<float, 3> k2{0.3f, 0.4f, 0.5f};

    auto resultLazy = FresnelConductor(0.5f, etaI, etaT, k);
    auto resultGeneric = FresnelConductor(0.5f, etaI2, etaT2, k2);
    auto resultLazyExpanded = FresnelConductorExpanded(0.5f, etaI, etaT, k);

    EXPECT_EQ(resultGeneric[0], resultLazy[0]);
    EXPECT_EQ(resultGeneric[1], resultLazy[1]);
    EXPECT_EQ(resultGeneric[2], resultLazy[2]);
    EXPECT_EQ(resultGeneric[0], resultLazyExpanded[0]);
    EXPECT_EQ(resultGeneric[1], resultLazyExpanded[1]);
    EXPECT_EQ(resultGeneric[2], resultLazyExpanded[2]);
}

TEST_F(VecteurLazyTests, Max) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{1, 2, 3};
    Vecteur<int, 3, VecteurBackend::Lazy> y{6, 5, 2};

    auto z = x.max(y);
    EXPECT_EQ(6, z[0]);
    EXPECT_EQ(5, z[1]);
    EXPECT_EQ(3, z[2]);
}

TEST_F(VecteurLazyTests, MaxScalar) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{1, 2, 3};

    auto z = x.max(2);
    EXPECT_EQ(2, z[0]);
    EXPECT_EQ(2, z[1]);
    EXPECT_EQ(3, z[2]);
}

TEST_F(VecteurLazyTests, Min) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{1, 2, 3};
    Vecteur<int, 3, VecteurBackend::Lazy> y{6, 5, 2};

    auto z = x.min(y);
    EXPECT_EQ(1, z[0]);
    EXPECT_EQ(2, z[1]);
    EXPECT_EQ(2, z[2]);
}

TEST_F(VecteurLazyTests, MinScalar) {
    Vecteur<int, 3, VecteurBackend::Lazy> x{1, 2, 3};

    auto z = x.min(2);
    EXPECT_EQ(1, z[0]);
    EXPECT_EQ(2, z[1]);
    EXPECT_EQ(2, z[2]);
}

TEST_F(VecteurLazyTests, MaxFloat) {
    Vecteur<double, 3, VecteurBackend::Lazy> x{1.1, 2.2, 3.3};
    Vecteur<double, 3, VecteurBackend::Lazy> y{6.6, 5.5, 2.2};

    auto z = x.max(y);
    EXPECT_NEAR(6.6, z[0], 1e-6);
    EXPECT_NEAR(5.5, z[1], 1e-6);
    EXPECT_NEAR(3.3, z[2], 1e-6);
}

TEST_F(VecteurLazyTests, MaxScalarFloat) {
    Vecteur<double, 3, VecteurBackend::Lazy> x{1.1, 2.2, 3.3};

    auto z = x.max(2.5);
    EXPECT_NEAR(2.5, z[0], 1e-6);
    EXPECT_NEAR(2.5, z[1], 1e-6);
    EXPECT_NEAR(3.3, z[2], 1e-6);
}

TEST_F(VecteurLazyTests, MinFloat) {
    Vecteur<double, 3, VecteurBackend::Lazy> x{1.1, 2.2, 3.3};
    Vecteur<double, 3, VecteurBackend::Lazy> y{6.6, 5.5, 2.2};

    auto z = x.min(y);
    EXPECT_NEAR(1.1, z[0], 1e-6);
    EXPECT_NEAR(2.2, z[1], 1e-6);
    EXPECT_NEAR(2.2, z[2], 1e-6);
}

TEST_F(VecteurLazyTests, MinScalarFloat) {
    Vecteur<double, 3, VecteurBackend::Lazy> x{1.1, 2.2, 3.3};

    auto z = x.min(2.5);
    EXPECT_NEAR(1.1, z[0], 1e-6);
    EXPECT_NEAR(2.2, z[1], 1e-6);
    EXPECT_NEAR(2.5, z[2], 1e-6);
}
