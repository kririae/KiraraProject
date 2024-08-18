#pragma once

#include "kira/Vecteur.h"

namespace kira::vecteur {
template <typename Spectrum>
inline Spectrum
FresnelConductor(float cosThetaI, Spectrum const &etaI, Spectrum const &etaT, Spectrum const &k) {
    cosThetaI = std::clamp<float>(cosThetaI, -1, 1);
    Spectrum const eta = etaT / etaI;
    Spectrum const etaK = k / etaI;

    auto const c2 = cosThetaI * cosThetaI;
    auto const si = 1 - c2;
    auto const eta2 = eta * eta;
    auto const etaK2 = etaK * etaK;
    auto const t0 = eta2 - etaK2 - si;
    auto const aPlusB = (t0 * t0 + eta2 * etaK2 * 4).sqrt();
    auto const t1 = aPlusB + c2;
    auto const a = ((aPlusB + t0) * 0.5F).sqrt();
    auto const t2 = a * cosThetaI * 2;
    auto const rs = (t1 - t2) / (t1 + t2);
    auto const t3 = si * si + aPlusB * c2;
    auto const t4 = t2 * si;
    auto const rp = rs * (t3 - t4) / (t3 + t4);

    return ((rp + rs) * 0.5F).eval();
}

template <typename Spectrum>
inline Spectrum FresnelConductorExpanded(
    float cosThetaI, Spectrum const &etaI, Spectrum const &etaT, Spectrum const &k
) {
    cosThetaI = std::clamp<float>(cosThetaI, -1, 1);
    Spectrum const eta = etaT / etaI;
    Spectrum const etaK = k / etaI;

    float const c2 = cosThetaI * cosThetaI;
    float const si = 1 - c2;
    Spectrum const eta2 = eta * eta;
    Spectrum const etaK2 = etaK * etaK;
    Spectrum const t0 = eta2 - etaK2 - si;
    Spectrum const aPlusB = (t0 * t0 + eta2 * etaK2 * 4).sqrt();
    Spectrum const t1 = aPlusB + c2;
    Spectrum const a = ((aPlusB + t0) * 0.5F).sqrt();
    Spectrum const t2 = a * cosThetaI * 2;
    Spectrum const rs = (t1 - t2) / (t1 + t2);
    Spectrum const t3 = si * si + aPlusB * c2;
    Spectrum const t4 = t2 * si;
    Spectrum const rp = rs * (t3 - t4) / (t3 + t4);

    return ((rp + rs) * 0.5F).eval();
}

/// The following tests data-parallellism in the math functions.
// TODO(krr): add data-level parallelism tests.
} // namespace kira::vecteur
