#include "kira/Vecteur.h"

using namespace kira;

int main() {
    float a0 = 0, a1 = 1, a2 = 2, a3 = 3;
    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> a{a0, a1, a2, a3};
    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> b{a0, a1, a2};
    Vecteur<float, std::dynamic_extent, VecteurBackend::Lazy> c;

    c = a + b;
    fmt::print("{} {} {}\n", c[0], c[1], c[2]);
}
