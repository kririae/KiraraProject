#include <iostream>
#include <span>

#include "ArithLib.h"
#include "kira/Vecteur.h"

using namespace kira;

using GenericVec4f = Vecteur<float, 3, VecteurBackend::Generic>;
using DynVec = Vecteur<float, std::dynamic_extent, VecteurBackend::Generic>;
using LazyVec3f = Vecteur<float, 3, VecteurBackend::Lazy>;

__attribute__((noinline)) LazyVec3f
fmad_vv(LazyVec3f const &a, LazyVec3f const &b, LazyVec3f const &c) {
    return (a * b + c).eval();
}

__attribute__((noinline)) LazyVec3f
fmad_vs(LazyVec3f const &a, LazyVec3f const &b, float const &c) {
    return (a * b + c).eval();
}

__attribute__((noinline)) LazyVec3f
fmad_vs_inv(LazyVec3f const &a, LazyVec3f const &b, float const &c) {
    return (c + a * b).eval();
}

__attribute__((noinline)) GenericVec4f
simd_add_generic_vec4f(GenericVec4f const &a, GenericVec4f const &b) {
    return a + b;
}

DynVec simd_add_generic_dynamic_vec4f(DynVec const &a, DynVec const &b, DynVec const &c) {
    return a * b + c;
}

void test_performance() {
    // Likely not a good benchmark, but it's a start.
    int const numIterations = 1000000;
    int const vectorSize = 64;

    DynVec a(vectorSize);
    DynVec b(vectorSize);
    DynVec c(vectorSize);

    for (int i = 0; i < vectorSize; ++i) {
        a[i] = static_cast<float>(i);
        b[i] = static_cast<float>(i * 2);
        c[i] = static_cast<float>(i * 3);
    }

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < numIterations; ++i) {
        DynVec result = FresnelConductorExpanded(0.5F, a, b, c);
        a[0] += result[0] * 0.000001f;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Time taken for " << numIterations << " iterations: " << duration.count()
              << " microseconds" << std::endl;
    std::cout << "Average time per operation: "
              << static_cast<double>(duration.count()) / numIterations << " microseconds"
              << std::endl;
}

int main() {
    test_performance();
#if 0
    float a0 = 0, a1 = 1, a2 = 2, a3 = 3;
    LazyVec3f a{a0, a1, a2};
    LazyVec3f b{a0, a1, a2};
    LazyVec3f c{a0, a1, a2};

    LazyVec3f d = fmad_vv(a, b, c);
    LazyVec3f e = fmad_vs(a, b, 1.0F);
    fmt::print("{} {} {}\n", c[0], c[1], c[2]);

    GenericVec4f f{a0};
    GenericVec4f g{a0};

    GenericVec4f h = simd_add_generic_vec4f(f, g);
    fmt::print("{} {} {} {}\n", h[0], h[1], h[2], h[3]);
#endif
}
