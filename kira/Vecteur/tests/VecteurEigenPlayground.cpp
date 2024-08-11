#include <gtest/gtest.h>

#include <Eigen/Dense>

#include "kira/Vecteur.h"

using namespace Eigen;
using namespace kira;

TEST(EigenTests, Init) {
    Eigen::Vector3f a(1.0f, 2.0f, 3.0f);
    Eigen::Vector3f b(4.0f, 5.0f, 6.0f);

    Eigen::Vector3f result = a + b;
    auto result2 = result + a;
}

TEST(EigenTests, Node) {
    Vecteur<int, 3, VecteurBackend::Generic> ag{3, 4, 5};
    Vecteur<int, 3, VecteurBackend::Generic> bg{1, 2, 3};

    Vecteur<int, 3, VecteurBackend::Lazy> al{3, 4, 5};
    Vecteur<int, 3, VecteurBackend::Lazy> bl{1, 2, 3};

    auto cl = al + bl;
    auto dl = cl + al;
    auto el = al + cl;
    auto fl = dl + el;

    auto cg = ag + bg;
    auto dg = cg + ag;
    auto eg = ag + cg;
    auto fg = dg + eg;

    EXPECT_EQ(fg.entry(0), fl.entry(0));
    EXPECT_EQ(fg.entry(1), fl.entry(1));
    EXPECT_EQ(fg.entry(2), fl.entry(2));
}

TEST(EigenTests, NodeCont) {
    Vecteur<int, 3, VecteurBackend::Lazy> al{3, 4, 5};
    Vecteur<int, 3, VecteurBackend::Lazy> bl{1, 2, 3};

    auto c = al + bl;
    auto d = bl + al;
}
