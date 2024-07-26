#include <iostream>
#include <ut2>

#include "kira/Vecteur.h"

namespace {
ut::suite vecteur = [] {
  using namespace ut;

  "zero_ctor"_test = [] {
    kira::Vecteur<int, 3> x{0};
    expect(0_i == x[0]);
    expect(0_i == x[1]);
    expect(0_i == x[2]);
  };

  "one_ctor"_test = [] {
    kira::Vecteur<int, 3> x{1};
    expect(1_i == x[0]);
    expect(1_i == x[1]);
    expect(1_i == x[2]);
  };

  "arb_ctor"_test = [] {
    kira::Vecteur<int, 3> x{1, 2, 3};
    expect(1_i == x[0]);
    expect(2_i == x[1]);
    expect(3_i == x[2]);
  };

  "xyz"_test = [] {
    kira::Vecteur<int, 3> x{1, 2, 3};
    expect(1_i == x.x());
    expect(2_i == x.y());
    expect(3_i == x.z());
  };

  "add"_test = [] {
    kira::Vecteur<int, 3> x{1, 2, 3};
    kira::Vecteur<int, 3> y{3, 2, 1};
    auto z = x + y;

    expect(z[0] == 4_i);
    expect(z[1] == 4_i);
    expect(z[2] == 4_i);

    auto w1 = x + 1;
    expect(w1.size() == 3_ul);
    expect(w1.x() == 2_i);
    expect(w1.y() == 3_i);
    expect(w1.z() == 4_i);

    auto w2 = 1 + x;
    expect(w2.size() == 3_ul);
    expect(w2.x() == 2_i);
    expect(w2.y() == 3_i);
    expect(w2.z() == 4_i);
  };

  "mul"_test = [] {
    kira::Vecteur<int, 3> x{1, 2, 3};
    kira::Vecteur<int, 3> y{2, 3, 4};

    auto z = x * y;
    expect(z[0] == 2_i);
    expect(z[1] == 6_i);
    expect(z[2] == 12_i);

    auto w1 = 2 * x;
    expect(w1.size() == 3_ul);
    expect(w1.x() == 2_i);
    expect(w1.y() == 4_i);
    expect(w1.z() == 6_i);

    auto w2 = x * 3;
    expect(w2.size() == 3_ul);
    expect(w2.x() == 3_i);
    expect(w2.y() == 6_i);
    expect(w2.z() == 9_i);
  };

  "mul_fp"_test = [] {
    kira::Vecteur<double, 3> x{1.1, 2.2, 3.3};
    kira::Vecteur<double, 3> y{2.5, 3.5, 4.5};

    auto z = x * y;
    expect((z[0] == 2.75_d)(.01)) << "z[0] should be close to 2.75";
    expect((z[1] == 7.70_d)(.01)) << "z[1] should be close to 7.70";
    expect((z[2] == 14.85_d)(.01)) << "z[2] should be close to 14.85";

    auto w1 = 2.5 * x;
    expect(w1.size() == 3_ul);
    expect((w1.x() == 2.75_d)(.01)) << "w1.x() should be close to 2.75";
    expect((w1.y() == 5.50_d)(.01)) << "w1.y() should be close to 5.50";
    expect((w1.z() == 8.25_d)(.01)) << "w1.z() should be close to 8.25";

    auto w2 = x * 3.5;
    expect(w2.size() == 3_ul);
    expect((w2.x() == 3.85_d)(.01)) << "w2.x() should be close to 3.85";
    expect((w2.y() == 7.70_d)(.01)) << "w2.y() should be close to 7.70";
    expect((w2.z() == 11.55_d)(.01)) << "w2.z() should be close to 11.55";
  };

  "div"_test = [] {
    kira::Vecteur<int, 3> x{6, 12, 18};
    kira::Vecteur<int, 3> y{2, 3, 4};

    auto z = x / y;
    expect(z[0] == 3_i);
    expect(z[1] == 4_i);
    expect(z[2] == 4_i);

    auto w1 = x / 2;
    expect(w1.size() == 3_ul);
    expect(w1.x() == 3_i);
    expect(w1.y() == 6_i);
    expect(w1.z() == 9_i);

    auto w2 = 36 / x;
    expect(w2.size() == 3_ul);
    expect(w2.x() == 6_i);
    expect(w2.y() == 3_i);
    expect(w2.z() == 2_i);
  };

  "div_fp"_test = [] {
    kira::Vecteur<double, 3> x{6.0, 12.0, 18.0};
    kira::Vecteur<double, 3> y{2.0, 3.0, 4.0};

    auto z = x / y;
    expect((z[0] == 3.0_d)(.01)) << "z[0] should be close to 3.0";
    expect((z[1] == 4.0_d)(.01)) << "z[1] should be close to 4.0";
    expect((z[2] == 4.5_d)(.01)) << "z[2] should be close to 4.5";

    auto w1 = x / 2.0;
    expect(w1.size() == 3_ul);
    expect((w1.x() == 3.0_d)(.01)) << "w1.x() should be close to 3.0";
    expect((w1.y() == 6.0_d)(.01)) << "w1.y() should be close to 6.0";
    expect((w1.z() == 9.0_d)(.01)) << "w1.z() should be close to 9.0";

    auto w2 = 36.0 / x;
    expect(w2.size() == 3_ul);
    expect((w2.x() == 6.0_d)(.01)) << "w2.x() should be close to 6.0";
    expect((w2.y() == 3.0_d)(.01)) << "w2.y() should be close to 3.0";
    expect((w2.z() == 2.0_d)(.01)) << "w2.z() should be close to 2.0";
  };
};
} // namespace

int main() {}
