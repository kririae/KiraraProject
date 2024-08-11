#pragma once

#include "kira/Vecteur.h"

namespace kira {
template <is_vecteur Spectrum>
inline Spectrum
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
    auto const a = ((apb + t0) * 0.5F).sqrt();
    auto const t2 = a * cos_theta_i * 2;
    auto const rs = (t1 - t2) / (t1 + t2);
    auto const t3 = si * si + apb * c2;
    auto const t4 = t2 * si;
    auto const rp = rs * (t3 - t4) / (t3 + t4);

    return ((rp + rs) * 0.5F).eval();
}

template <is_vecteur Spectrum>
inline Spectrum FresnelConductorExpanded(
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
    Spectrum const a = ((apb + t0) * 0.5F).sqrt();
    Spectrum const t2 = a * cos_theta_i * 2;
    Spectrum const rs = (t1 - t2) / (t1 + t2);
    Spectrum const t3 = si * si + apb * c2;
    Spectrum const t4 = t2 * si;
    Spectrum const rp = rs * (t3 - t4) / (t3 + t4);

    return ((rp + rs) * 0.5F).eval();
}
} // namespace kira
