#pragma once

#include <Core/Math.h>

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace krd::ipc {
///
/// \brief Epsilon multipliers for CCD roots and predicates.
///
/// Callers may pass the structural aggregate as a non-type template parameter. Each
/// field scales machine epsilon for the arithmetic type.
///
struct CCDConfig {
    /// \brief Polynomial coefficient, root, discriminant, and time comparison scale.
    int rootToleranceScale = 2048;

    /// \brief Initial edge and triangle degeneracy scale.
    int degenerateToleranceScale = 1024;

    /// \brief Closed point-triangle barycentric bound scale.
    int barycentricToleranceScale = 64;

    /// \brief Zero-distance edge-edge validation scale.
    int segmentDistanceToleranceScale = 4096;

    /// \brief True when all tolerance scales are positive.
    [[nodiscard]] constexpr bool valid() const {
        return rootToleranceScale > 0 && degenerateToleranceScale > 0 &&
               barycentricToleranceScale > 0 && segmentDistanceToleranceScale > 0;
    }

    /// \brief Polynomial, root, discriminant, and time epsilon for `Real`.
    template <typename Real> constexpr Real rootTolerance() const {
        return Real(rootToleranceScale) * std::numeric_limits<Real>::epsilon();
    }

    /// \brief Initial degeneracy epsilon for `Real`.
    template <typename Real> constexpr Real degenerateTolerance() const {
        return Real(degenerateToleranceScale) * std::numeric_limits<Real>::epsilon();
    }

    /// \brief Closed barycentric-bound epsilon for `Real`.
    template <typename Real> constexpr Real barycentricTolerance() const {
        return Real(barycentricToleranceScale) * std::numeric_limits<Real>::epsilon();
    }

    /// \brief Edge-edge zero-distance epsilon for `Real`.
    template <typename Real> constexpr Real segmentDistanceTolerance() const {
        return Real(segmentDistanceToleranceScale) * std::numeric_limits<Real>::epsilon();
    }
};

namespace detail {
static_assert(CCDConfig{}.valid());

// One cubic coplanarity polynomial plus {0, 1}.
inline constexpr int NonCoplanarTimeCapacity = 3 + 2;

// Three triangle edges, each quadratic, plus {0, 1}.
inline constexpr int PointTriangleCoplanarTimeCapacity = 3 * 2 + 2;

// Four orientation quadratics, four endpoint pairs times two coordinates, plus {0, 1}.
inline constexpr int EdgeEdgeCoplanarTimeCapacity = 4 * 2 + 4 * 2 + 2;

static_assert(NonCoplanarTimeCapacity == 5);
static_assert(PointTriangleCoplanarTimeCapacity == 8);
static_assert(EdgeEdgeCoplanarTimeCapacity == 18);

/// \brief Sorted fixed-capacity set of unique candidate times in [0, 1].
template <typename Real, int Capacity> struct TimeCandidates {
    std::array<Real, Capacity> values{};
    int size = 0;

    Real &operator[](int i) { return values[static_cast<size_t>(i)]; }
    Real const &operator[](int i) const { return values[static_cast<size_t>(i)]; }

    void push(Real value, Real eps) {
        if (value < -eps || value > Real(1) + eps)
            return;

        value = std::max(Real(0), std::min(Real(1), value));
        for (int i = 0; i < size; ++i)
            if (std::abs(values[static_cast<size_t>(i)] - value) <= eps)
                return;

        if (size >= Capacity)
            return;

        int pos = size;
        while (pos > 0 && value < values[static_cast<size_t>(pos - 1)]) {
            values[static_cast<size_t>(pos)] = values[static_cast<size_t>(pos - 1)];
            --pos;
        }
        values[static_cast<size_t>(pos)] = value;
        ++size;
    }
};

template <typename Real, CCDConfig Cfg, size_t N>
Real polynomialTolerance(std::array<Real, N> const &coeffs) {
    // floor at 1 so a zero polynomial still gets non-zero tolerance
    Real scale = Real(1);
    for (size_t i = 0; i < N; ++i)
        scale = std::max(scale, std::abs(coeffs[i]));
    return scale * Cfg.rootTolerance<Real>();
}

template <typename Real, CCDConfig Cfg, size_t N>
bool polynomialIsZero(std::array<Real, N> const &coeffs) {
    Real const eps = polynomialTolerance<Real, Cfg>(coeffs);
    for (size_t i = 0; i < N; ++i)
        if (std::abs(coeffs[i]) > eps)
            return false;
    return true;
}

template <typename Real, int Capacity, CCDConfig Cfg>
void addLinearRoot(Real c0, Real c1, TimeCandidates<Real, Capacity> &roots, Real eps) {
    if (std::abs(c1) > eps)
        roots.push(-c0 / c1, Cfg.rootTolerance<Real>());
}

template <typename Real, int Capacity, CCDConfig Cfg>
void addQuadraticRoots(
    std::array<Real, 3> const &coeffs, TimeCandidates<Real, Capacity> &roots, Real eps
) {
    Real const a = coeffs[2];
    Real const b = coeffs[1];
    Real const c = coeffs[0];
    if (std::abs(a) <= eps) {
        addLinearRoot<Real, Capacity, Cfg>(c, b, roots, eps);
        return;
    }

    Real const disc = b * b - Real(4) * a * c;
    Real const discEps =
        std::max(Real(1), std::abs(b * b) + std::abs(Real(4) * a * c)) * Cfg.rootTolerance<Real>();
    if (disc < -discEps)
        return;

    Real const timeEps = Cfg.rootTolerance<Real>();
    if (std::abs(disc) <= discEps) {
        roots.push(-b / (Real(2) * a), timeEps);
        return;
    }

    Real const sqrtDisc = std::sqrt(disc);
    Real const signedSqrt = b < Real(0) ? -sqrtDisc : sqrtDisc;
    Real const q = Real(-0.5) * (b + signedSqrt);
    // Citardauq second root is c / q; when q ~ 0 that division may overflow,
    // so fall back to the textbook formula for both roots.
    if (std::abs(q) <= eps) {
        Real const invDenom = Real(0.5) / a;
        roots.push((-b - sqrtDisc) * invDenom, timeEps);
        roots.push((-b + sqrtDisc) * invDenom, timeEps);
        return;
    }

    roots.push(q / a, timeEps);
    roots.push(c / q, timeEps);
}

///
/// \brief Add real roots of c0 + c1*t + c2*t^2 + c3*t^3 inside [0, 1].
///
/// Coefficient order: c0, c1, c2, c3. Linear and quadratic degeneracies use the
/// same coefficient tolerance. Cardano handles the cubic case, and \p roots
/// merges repeated roots.
///
/// \tparam Real Arithmetic scalar.
/// \tparam Capacity Fixed capacity of \p roots.
/// \param coeffs Cubic coefficients in ascending power order.
/// \param roots Destination sorted candidate set.
///
template <typename Real, int Capacity, CCDConfig Cfg>
void addCubicRoots(std::array<Real, 4> const &coeffs, TimeCandidates<Real, Capacity> &roots) {
    Real const eps = polynomialTolerance<Real, Cfg>(coeffs);
    Real const timeEps = Cfg.rootTolerance<Real>();
    if (std::abs(coeffs[3]) <= eps) {
        addQuadraticRoots<Real, Capacity, Cfg>(
            std::array<Real, 3>{coeffs[0], coeffs[1], coeffs[2]}, roots, eps
        );
        return;
    }

    Real const a = coeffs[2] / coeffs[3];
    Real const b = coeffs[1] / coeffs[3];
    Real const c = coeffs[0] / coeffs[3];
    Real const third = Real(1) / Real(3);
    Real const p = b - a * a * third;
    Real const q = (Real(2) * a * a * a) / Real(27) - (a * b) * third + c;
    Real const halfQ = q * Real(0.5);
    Real const thirdP = p * third;
    Real const disc = halfQ * halfQ + thirdP * thirdP * thirdP;
    Real const discScale =
        std::max(Real(1), std::abs(halfQ * halfQ) + std::abs(thirdP * thirdP * thirdP));
    Real const discEps = discScale * Cfg.rootTolerance<Real>();
    Real const shift = -a * third;

    if (disc > discEps) {
        Real const sqrtDisc = std::sqrt(disc);
        roots.push(std::cbrt(-halfQ + sqrtDisc) + std::cbrt(-halfQ - sqrtDisc) + shift, timeEps);
        return;
    }

    if (std::abs(disc) <= discEps) {
        Real const u = std::cbrt(-halfQ);
        roots.push(Real(2) * u + shift, timeEps);
        roots.push(-u + shift, timeEps);
        return;
    }

    // mathematically unreachable when disc < 0 (p < 0 is required for
    // three real roots); this branch exists as a numerical safety net only
    if (p >= Real(0)) {
        roots.push(shift, timeEps);
        return;
    }

    Real const pi = std::acos(Real(-1));
    Real const radius = Real(2) * std::sqrt(-p * third);
    Real cosArg = -halfQ / std::sqrt(-(thirdP * thirdP * thirdP));
    cosArg = std::max(Real(-1), std::min(Real(1), cosArg));
    Real const angle = std::acos(cosArg);
    roots.push(radius * std::cos(angle * third) + shift, timeEps);
    roots.push(radius * std::cos((angle + Real(2) * pi) * third) + shift, timeEps);
    roots.push(radius * std::cos((angle + Real(4) * pi) * third) + shift, timeEps);
}

///
/// \brief Oriented-volume polynomial for four moving points.
///
/// The value is dot(pa(t) - pb(t), cross(pc(t) - pb(t), pd(t) - pb(t))).
/// Coefficient order: c0, c1, c2, c3.
///
/// \tparam T Input scalar type.
/// \tparam Real Arithmetic scalar.
/// \param pa Point a at t = 0.
/// \param dpa Point a displacement over [0, 1].
/// \param pb Point b at t = 0.
/// \param dpb Point b displacement over [0, 1].
/// \param pc Point c at t = 0.
/// \param dpc Point c displacement over [0, 1].
/// \param pd Point d at t = 0.
/// \param dpd Point d displacement over [0, 1].
/// \return Cubic coefficients in ascending power order.
///
template <typename T, typename Real>
std::array<Real, 4> coplanarityPolynomial(
    Vector3<T> const &pa, Vector3<T> const &dpa, Vector3<T> const &pb, Vector3<T> const &dpb,
    Vector3<T> const &pc, Vector3<T> const &dpc, Vector3<T> const &pd, Vector3<T> const &dpd
) {
    Vector3<Real> const q0 = (pa - pb).template cast<Real>();
    Vector3<Real> const qv = (dpa - dpb).template cast<Real>();
    Vector3<Real> const r0 = (pc - pb).template cast<Real>();
    Vector3<Real> const rv = (dpc - dpb).template cast<Real>();
    Vector3<Real> const s0 = (pd - pb).template cast<Real>();
    Vector3<Real> const sv = (dpd - dpb).template cast<Real>();

    Vector3<Real> const c0 = r0.cross(s0);
    Vector3<Real> const c1 = r0.cross(sv) + rv.cross(s0);
    Vector3<Real> const c2 = rv.cross(sv);

    return {
        q0.dot(c0),
        qv.dot(c0) + q0.dot(c1),
        qv.dot(c1) + q0.dot(c2),
        qv.dot(c2),
    };
}

/// \brief True when a closed edge has near-zero length.
template <typename Real, CCDConfig Cfg>
bool edgeDegenerate(Vector3<Real> const &a, Vector3<Real> const &b) {
    // scale by endpoint magnitude so that small edges far from the origin
    // (e.g. near (1e10, 0, 0)) are not falsely flagged as degenerate
    Real const scale = std::max(Real(1), std::max(a.squaredNorm(), b.squaredNorm()));
    Real const eps = Cfg.degenerateTolerance<Real>();
    return (b - a).squaredNorm() <= scale * eps * eps;
}

/// \brief True when a closed triangle has near-zero area.
template <typename Real, CCDConfig Cfg>
bool triangleDegenerate(Vector3<Real> const &a, Vector3<Real> const &b, Vector3<Real> const &c) {
    Vector3<Real> const ab = b - a;
    Vector3<Real> const ac = c - a;
    Vector3<Real> const bc = c - b;
    Real const length2 =
        std::max(Real(1), std::max(ab.squaredNorm(), std::max(ac.squaredNorm(), bc.squaredNorm())));
    Real const eps = Cfg.degenerateTolerance<Real>();
    return ab.cross(ac).squaredNorm() <= length2 * length2 * eps * eps;
}

/// \brief Closed point-triangle predicate; degenerate triangles return false.
template <typename Real, CCDConfig Cfg>
bool pointInTriangle(
    Vector3<Real> const &p, Vector3<Real> const &a, Vector3<Real> const &b, Vector3<Real> const &c
) {
    if (triangleDegenerate<Real, Cfg>(a, b, c))
        return false;

    Vector3<Real> const v0 = b - a;
    Vector3<Real> const v1 = c - a;
    Vector3<Real> const v2 = p - a;
    Real const d00 = v0.dot(v0);
    Real const d01 = v0.dot(v1);
    Real const d11 = v1.dot(v1);
    Real const d20 = v2.dot(v0);
    Real const d21 = v2.dot(v1);
    Real const denom = d00 * d11 - d01 * d01;
    // near-zero denom means the projection basis (v0, v1) is nearly
    // degenerate, not a barycentric boundary. Use degenerate tolerance
    // instead of barycentric tolerance to express that fact
    Real const denomEps = std::max(Real(1), std::abs(denom)) * Cfg.degenerateTolerance<Real>();
    if (std::abs(denom) <= denomEps)
        return false;

    Real const v = (d11 * d20 - d01 * d21) / denom;
    Real const w = (d00 * d21 - d01 * d20) / denom;
    Real const tol = Cfg.barycentricTolerance<Real>();
    return v >= -tol && w >= -tol && v + w <= Real(1) + tol;
}

/// \brief Squared distance between closed segments; degenerate segments become points.
template <typename Real, CCDConfig Cfg>
Real segmentSegmentDistance2(
    Vector3<Real> const &p1, Vector3<Real> const &q1, Vector3<Real> const &p2,
    Vector3<Real> const &q2
) {
    Vector3<Real> const d1 = q1 - p1;
    Vector3<Real> const d2 = q2 - p2;
    Vector3<Real> const r = p1 - p2;
    Real const a = d1.dot(d1);
    Real const e = d2.dot(d2);
    Real const f = d2.dot(r);
    Real const eps = Cfg.degenerateTolerance<Real>();

    Real s = 0;
    Real t = 0;
    if (a <= eps && e <= eps)
        return (p1 - p2).squaredNorm();
    if (a <= eps) {
        t = std::max(Real(0), std::min(Real(1), f / e));
    } else {
        Real const c = d1.dot(r);
        if (e <= eps) {
            s = std::max(Real(0), std::min(Real(1), -c / a));
        } else {
            Real const b = d1.dot(d2);
            Real const denom = a * e - b * b;
            if (std::abs(denom) > eps)
                s = std::max(Real(0), std::min(Real(1), (b * f - c * e) / denom));
            t = (b * s + f) / e;
            if (t < Real(0)) {
                t = Real(0);
                s = std::max(Real(0), std::min(Real(1), -c / a));
            } else if (t > Real(1)) {
                t = Real(1);
                s = std::max(Real(0), std::min(Real(1), (b - c) / a));
            }
        }
    }

    Vector3<Real> const c1 = p1 + d1 * s;
    Vector3<Real> const c2 = p2 + d2 * t;
    return (c1 - c2).squaredNorm();
}

/// \brief Closed edge-edge predicate in 3D; degenerate edges return false.
template <typename Real, CCDConfig Cfg>
bool edgesIntersect3D(
    Vector3<Real> const &a0, Vector3<Real> const &a1, Vector3<Real> const &b0,
    Vector3<Real> const &b1
) {
    if (edgeDegenerate<Real, Cfg>(a0, a1) || edgeDegenerate<Real, Cfg>(b0, b1))
        return false;

    Real const lenScale =
        std::max(Real(1), std::max((a1 - a0).squaredNorm(), (b1 - b0).squaredNorm()));
    Real const eps = Cfg.segmentDistanceTolerance<Real>();
    Real const tol2 = lenScale * eps * eps;
    return segmentSegmentDistance2<Real, Cfg>(a0, a1, b0, b1) <= tol2;
}

/// \brief Axis dropped by the largest-area 2D projection.
template <typename Real> int projectionAxis(Vector3<Real> const &normal) {
    Vector3<Real> const a = normal.cwiseAbs();
    if (a.x() >= a.y() && a.x() >= a.z())
        return 0;
    if (a.y() >= a.z())
        return 1;
    return 2;
}

/// \brief Axis dropped while preserving the strongest line coordinate.
template <typename Real> int projectionAxisFromDirection(Vector3<Real> const &direction) {
    Vector3<Real> const a = direction.cwiseAbs();
    if (a.x() <= a.y() && a.x() <= a.z())
        return 0;
    if (a.y() <= a.z())
        return 1;
    return 2;
}

/// \brief Projected coordinate after dropping one 3D axis.
template <int DroppedAxis, int ProjectedAxis, typename Real>
Real projectedCoord(Vector3<Real> const &v) {
    static_assert(DroppedAxis >= 0 && DroppedAxis < 3);
    static_assert(ProjectedAxis >= 0 && ProjectedAxis < 2);
    if constexpr (DroppedAxis == 0 && ProjectedAxis == 0)
        return v.y();
    if constexpr (DroppedAxis == 0 && ProjectedAxis == 1)
        return v.z();
    if constexpr (DroppedAxis == 1 && ProjectedAxis == 0)
        return v.x();
    if constexpr (DroppedAxis == 1 && ProjectedAxis == 1)
        return v.z();
    if constexpr (ProjectedAxis == 0)
        return v.x();
    return v.y();
}

///
/// \brief Projected 2D orientation polynomial for three moving points.
///
/// The dropped axis selects the projection plane at compile time. The polynomial
/// is orient2d(a(t), b(t), c(t)) after projection. Coefficient order: c0, c1,
/// c2.
///
/// \tparam DroppedAxis Coordinate removed from the 3D projection.
/// \tparam Real Arithmetic scalar.
/// \param a0 Point a at t = 0.
/// \param da Point a displacement over [0, 1].
/// \param b0 Point b at t = 0.
/// \param db Point b displacement over [0, 1].
/// \param c0 Point c at t = 0.
/// \param dc Point c displacement over [0, 1].
/// \return Quadratic coefficients in ascending power order.
///
template <int DroppedAxis, typename Real>
std::array<Real, 3> orientation2DPolynomial(
    Vector3<Real> const &a0, Vector3<Real> const &da, Vector3<Real> const &b0,
    Vector3<Real> const &db, Vector3<Real> const &c0, Vector3<Real> const &dc
) {
    Vector3<Real> const r0 = b0 - a0;
    Vector3<Real> const rv = db - da;
    Vector3<Real> const s0 = c0 - a0;
    Vector3<Real> const sv = dc - da;
    auto cross2 = [](Vector3<Real> const &u, Vector3<Real> const &v) {
        return projectedCoord<DroppedAxis, 0>(u) * projectedCoord<DroppedAxis, 1>(v) -
               projectedCoord<DroppedAxis, 1>(u) * projectedCoord<DroppedAxis, 0>(v);
    };
    return {
        cross2(r0, s0),
        cross2(r0, sv) + cross2(rv, s0),
        cross2(rv, sv),
    };
}

/// \brief Coplanar point-triangle fallback; interval hits report the lower endpoint.
template <int DroppedAxis, typename Real, CCDConfig Cfg>
bool coplanarPointTriangle(
    Vector3<Real> const &pr, Vector3<Real> const &dr, Vector3<Real> const &p1,
    Vector3<Real> const &dp1, Vector3<Real> const &p2, Vector3<Real> const &dp2,
    Vector3<Real> const &p3, Vector3<Real> const &dp3, Real &toi
) {
    TimeCandidates<Real, PointTriangleCoplanarTimeCapacity> times;
    Real const eps = Cfg.rootTolerance<Real>();
    times.push(Real(0), eps);
    times.push(Real(1), eps);
    auto addOrientation = [&](Vector3<Real> const &a0, Vector3<Real> const &da,
                              Vector3<Real> const &b0, Vector3<Real> const &db,
                              Vector3<Real> const &c0, Vector3<Real> const &dc) {
        auto const coeffs = orientation2DPolynomial<DroppedAxis>(a0, da, b0, db, c0, dc);
        addQuadraticRoots<Real, PointTriangleCoplanarTimeCapacity, Cfg>(
            coeffs, times, polynomialTolerance<Real, Cfg>(coeffs)
        );
    };
    addOrientation(p1, dp1, p2, dp2, pr, dr);
    addOrientation(p2, dp2, p3, dp3, pr, dr);
    addOrientation(p3, dp3, p1, dp1, pr, dr);

    auto intersects = [&](Real t) {
        Vector3<Real> const r = pr + dr * t;
        Vector3<Real> const a = p1 + dp1 * t;
        Vector3<Real> const b = p2 + dp2 * t;
        Vector3<Real> const c = p3 + dp3 * t;
        return pointInTriangle<Real, Cfg>(r, a, b, c);
    };

    for (int i = 0; i < times.size; ++i) {
        if (intersects(times[i])) {
            toi = times[i];
            return true;
        }
    }
    for (int i = 1; i < times.size; ++i) {
        Real const lo = times[i - 1];
        Real const hi = times[i];
        if (hi - lo <= eps)
            continue;
        if (intersects((lo + hi) * Real(0.5))) {
            toi = lo;
            return true;
        }
    }
    return false;
}

/// \brief Coplanar edge-edge fallback; interval hits report the lower endpoint.
template <int DroppedAxis, typename Real, CCDConfig Cfg>
bool coplanarEdgeEdge(
    Vector3<Real> const &ea0, Vector3<Real> const &dea0, Vector3<Real> const &ea1,
    Vector3<Real> const &dea1, Vector3<Real> const &eb0, Vector3<Real> const &deb0,
    Vector3<Real> const &eb1, Vector3<Real> const &deb1, Real &toi
) {
    TimeCandidates<Real, EdgeEdgeCoplanarTimeCapacity> times;
    Real const eps = Cfg.rootTolerance<Real>();
    times.push(Real(0), eps);
    times.push(Real(1), eps);

    auto addOrientation = [&](Vector3<Real> const &a0, Vector3<Real> const &da,
                              Vector3<Real> const &b0, Vector3<Real> const &db,
                              Vector3<Real> const &c0, Vector3<Real> const &dc) {
        auto const coeffs = orientation2DPolynomial<DroppedAxis>(a0, da, b0, db, c0, dc);
        addQuadraticRoots<Real, EdgeEdgeCoplanarTimeCapacity, Cfg>(
            coeffs, times, polynomialTolerance<Real, Cfg>(coeffs)
        );
    };
    addOrientation(ea0, dea0, ea1, dea1, eb0, deb0);
    addOrientation(ea0, dea0, ea1, dea1, eb1, deb1);
    addOrientation(eb0, deb0, eb1, deb1, ea0, dea0);
    addOrientation(eb0, deb0, eb1, deb1, ea1, dea1);

    auto addCoordinateEquality = [&](Vector3<Real> const &a0, Vector3<Real> const &da,
                                     Vector3<Real> const &b0, Vector3<Real> const &db) {
        Vector3<Real> const c0 = a0 - b0;
        Vector3<Real> const cv = da - db;
        addLinearRoot<Real, EdgeEdgeCoplanarTimeCapacity, Cfg>(
            projectedCoord<DroppedAxis, 0>(c0), projectedCoord<DroppedAxis, 0>(cv), times, eps
        );
        addLinearRoot<Real, EdgeEdgeCoplanarTimeCapacity, Cfg>(
            projectedCoord<DroppedAxis, 1>(c0), projectedCoord<DroppedAxis, 1>(cv), times, eps
        );
    };
    addCoordinateEquality(ea0, dea0, eb0, deb0);
    addCoordinateEquality(ea0, dea0, eb1, deb1);
    addCoordinateEquality(ea1, dea1, eb0, deb0);
    addCoordinateEquality(ea1, dea1, eb1, deb1);

    auto intersects = [&](Real t) {
        Vector3<Real> const a0 = ea0 + dea0 * t;
        Vector3<Real> const a1 = ea1 + dea1 * t;
        Vector3<Real> const b0 = eb0 + deb0 * t;
        Vector3<Real> const b1 = eb1 + deb1 * t;
        return edgesIntersect3D<Real, Cfg>(a0, a1, b0, b1);
    };

    for (int i = 0; i < times.size; ++i) {
        if (intersects(times[i])) {
            toi = times[i];
            return true;
        }
    }
    for (int i = 1; i < times.size; ++i) {
        Real const lo = times[i - 1];
        Real const hi = times[i];
        if (hi - lo <= eps)
            continue;
        if (intersects((lo + hi) * Real(0.5))) {
            toi = lo;
            return true;
        }
    }
    return false;
}
} // namespace detail

///
/// \brief Continuous point-triangle intersection over normalized time t in [0, 1].
///
/// Inputs use linear motion x(t) = x0 + t * dx. The triangle is closed, including
/// edges and vertices. Hits write the earliest contact time to \p toi. Misses and
/// triangles degenerate at t = 0 leave \p toi unchanged.
///
/// \tparam T Input scalar type.
/// \tparam Real Internal arithmetic scalar. Defaults to T.
/// \tparam Cfg Tolerance policy. Defaults to CCDConfig{}.
/// \param pr Point position at t = 0.
/// \param dr Point displacement over [0, 1].
/// \param p1 Triangle vertex 1 at t = 0.
/// \param dp1 Triangle vertex 1 displacement over [0, 1].
/// \param p2 Triangle vertex 2 at t = 0.
/// \param dp2 Triangle vertex 2 displacement over [0, 1].
/// \param p3 Triangle vertex 3 at t = 0.
/// \param dp3 Triangle vertex 3 displacement over [0, 1].
/// \param toi Earliest contact time in [0, 1] on hit.
/// \return True when contact exists in the closed interval [0, 1].
///
template <typename T, typename Real = T, CCDConfig Cfg = {}>
bool CCDPointTriangle(
    Vector3<T> const &pr, Vector3<T> const &dr,  //
    Vector3<T> const &p1, Vector3<T> const &dp1, //
    Vector3<T> const &p2, Vector3<T> const &dp2, //
    Vector3<T> const &p3, Vector3<T> const &dp3, //
    Real &toi
) {
    static_assert(Cfg.valid(), "CCDConfig tolerance multipliers must be positive.");

    Vector3<Real> const r0 = pr.template cast<Real>();
    Vector3<Real> const vr = dr.template cast<Real>();
    Vector3<Real> const a0 = p1.template cast<Real>();
    Vector3<Real> const va = dp1.template cast<Real>();
    Vector3<Real> const b0 = p2.template cast<Real>();
    Vector3<Real> const vb = dp2.template cast<Real>();
    Vector3<Real> const c0 = p3.template cast<Real>();
    Vector3<Real> const vc = dp3.template cast<Real>();

    if (detail::triangleDegenerate<Real, Cfg>(a0, b0, c0))
        return false;

    auto const coeffs = detail::coplanarityPolynomial<T, Real>(pr, dr, p1, dp1, p2, dp2, p3, dp3);
    if (detail::polynomialIsZero<Real, Cfg>(coeffs)) {
        auto normalAt = [&](Real t) {
            Vector3<Real> const a = a0 + va * t;
            Vector3<Real> const b = b0 + vb * t;
            Vector3<Real> const c = c0 + vc * t;
            return (b - a).cross(c - a);
        };
        Vector3<Real> normal = normalAt(Real(0));
        Vector3<Real> const middleNormal = normalAt(Real(0.5));
        Vector3<Real> const finalNormal = normalAt(Real(1));
        if (middleNormal.squaredNorm() > normal.squaredNorm())
            normal = middleNormal;
        if (finalNormal.squaredNorm() > normal.squaredNorm())
            normal = finalNormal;
        int const axis = detail::projectionAxis(normal);
        if (axis == 0)
            return detail::coplanarPointTriangle<0, Real, Cfg>(r0, vr, a0, va, b0, vb, c0, vc, toi);
        if (axis == 1)
            return detail::coplanarPointTriangle<1, Real, Cfg>(r0, vr, a0, va, b0, vb, c0, vc, toi);
        return detail::coplanarPointTriangle<2, Real, Cfg>(r0, vr, a0, va, b0, vb, c0, vc, toi);
    }

    detail::TimeCandidates<Real, detail::NonCoplanarTimeCapacity> times;
    detail::addCubicRoots<Real, detail::NonCoplanarTimeCapacity, Cfg>(coeffs, times);
    Real const eps = Cfg.rootTolerance<Real>();
    times.push(Real(0), eps);
    times.push(Real(1), eps);

    auto coplanarityAt = [&](Real t) {
        return ((coeffs[3] * t + coeffs[2]) * t + coeffs[1]) * t + coeffs[0];
    };

    for (int i = 0; i < times.size; ++i) {
        Real const t = times[i];
        if (std::abs(coplanarityAt(t)) > detail::polynomialTolerance<Real, Cfg>(coeffs))
            continue;
        Vector3<Real> const r = r0 + vr * t;
        Vector3<Real> const a = a0 + va * t;
        Vector3<Real> const b = b0 + vb * t;
        Vector3<Real> const c = c0 + vc * t;
        if (detail::pointInTriangle<Real, Cfg>(r, a, b, c)) {
            toi = t;
            return true;
        }
    }
    return false;
}

///
/// \brief Continuous edge-edge intersection over normalized time t in [0, 1].
///
/// Inputs use linear motion x(t) = x0 + t * dx. Both segments are closed. Hits
/// write the earliest contact time to \p toi. Misses and edges degenerate at
/// t = 0 leave \p toi unchanged.
///
/// \tparam T Input scalar type.
/// \tparam Real Internal arithmetic scalar. Defaults to T.
/// \tparam Cfg Tolerance policy. Defaults to CCDConfig{}.
/// \param ea0 First endpoint of edge a at t = 0.
/// \param dea0 First endpoint displacement of edge a over [0, 1].
/// \param ea1 Second endpoint of edge a at t = 0.
/// \param dea1 Second endpoint displacement of edge a over [0, 1].
/// \param eb0 First endpoint of edge b at t = 0.
/// \param deb0 First endpoint displacement of edge b over [0, 1].
/// \param eb1 Second endpoint of edge b at t = 0.
/// \param deb1 Second endpoint displacement of edge b over [0, 1].
/// \param toi Earliest contact time in [0, 1] on hit.
/// \return True when contact exists in the closed interval [0, 1].
///
template <typename T, typename Real = T, CCDConfig Cfg = {}>
bool CCDEdgeEdge(
    Vector3<T> const &ea0, Vector3<T> const &dea0, //
    Vector3<T> const &ea1, Vector3<T> const &dea1, //
    Vector3<T> const &eb0, Vector3<T> const &deb0, //
    Vector3<T> const &eb1, Vector3<T> const &deb1, //
    Real &toi
) {
    static_assert(Cfg.valid(), "CCDConfig tolerance multipliers must be positive.");

    Vector3<Real> const a0 = ea0.template cast<Real>();
    Vector3<Real> const va0 = dea0.template cast<Real>();
    Vector3<Real> const a1 = ea1.template cast<Real>();
    Vector3<Real> const va1 = dea1.template cast<Real>();
    Vector3<Real> const b0 = eb0.template cast<Real>();
    Vector3<Real> const vb0 = deb0.template cast<Real>();
    Vector3<Real> const b1 = eb1.template cast<Real>();
    Vector3<Real> const vb1 = deb1.template cast<Real>();

    if (detail::edgeDegenerate<Real, Cfg>(a0, a1) || detail::edgeDegenerate<Real, Cfg>(b0, b1))
        return false;

    auto const coeffs =
        detail::coplanarityPolynomial<T, Real>(ea0, dea0, eb0, deb0, ea1, dea1, eb1, deb1);
    if (detail::polynomialIsZero<Real, Cfg>(coeffs)) {
        auto normalAt = [&](Real t) {
            Vector3<Real> const aStart = a0 + va0 * t;
            Vector3<Real> const aEnd = a1 + va1 * t;
            Vector3<Real> const bStart = b0 + vb0 * t;
            Vector3<Real> const bEnd = b1 + vb1 * t;
            return (aEnd - aStart).cross(bEnd - bStart);
        };
        Vector3<Real> normal = normalAt(Real(0));
        Vector3<Real> const middleNormal = normalAt(Real(0.5));
        Vector3<Real> const finalNormal = normalAt(Real(1));
        if (middleNormal.squaredNorm() > normal.squaredNorm())
            normal = middleNormal;
        if (finalNormal.squaredNorm() > normal.squaredNorm())
            normal = finalNormal;

        int axis = detail::projectionAxis(normal);
        Real const edgeScale =
            std::max(Real(1), std::max((a1 - a0).squaredNorm(), (b1 - b0).squaredNorm()));
        Real const degenerateEps = Cfg.degenerateTolerance<Real>();
        // when edges are nearly parallel the cross-product normal vanishes
        // and cannot guide axis selection; use the strongest edge direction
        // instead
        if (normal.squaredNorm() <= edgeScale * edgeScale * degenerateEps * degenerateEps) {
            Vector3<Real> direction = a1 - a0;
            auto chooseDirection = [&](Vector3<Real> const &candidate) {
                if (candidate.squaredNorm() > direction.squaredNorm())
                    direction = candidate;
            };
            chooseDirection(b1 - b0);
            chooseDirection((a1 + va1 * Real(0.5)) - (a0 + va0 * Real(0.5)));
            chooseDirection((b1 + vb1 * Real(0.5)) - (b0 + vb0 * Real(0.5)));
            chooseDirection((a1 + va1) - (a0 + va0));
            chooseDirection((b1 + vb1) - (b0 + vb0));
            axis = detail::projectionAxisFromDirection(direction);
        }

        if (axis == 0)
            return detail::coplanarEdgeEdge<0, Real, Cfg>(a0, va0, a1, va1, b0, vb0, b1, vb1, toi);
        if (axis == 1)
            return detail::coplanarEdgeEdge<1, Real, Cfg>(a0, va0, a1, va1, b0, vb0, b1, vb1, toi);
        return detail::coplanarEdgeEdge<2, Real, Cfg>(a0, va0, a1, va1, b0, vb0, b1, vb1, toi);
    }

    detail::TimeCandidates<Real, detail::NonCoplanarTimeCapacity> times;
    detail::addCubicRoots<Real, detail::NonCoplanarTimeCapacity, Cfg>(coeffs, times);
    Real const eps = Cfg.rootTolerance<Real>();
    times.push(Real(0), eps);
    times.push(Real(1), eps);

    auto coplanarityAt = [&](Real t) {
        return ((coeffs[3] * t + coeffs[2]) * t + coeffs[1]) * t + coeffs[0];
    };

    for (int i = 0; i < times.size; ++i) {
        Real const t = times[i];
        if (std::abs(coplanarityAt(t)) > detail::polynomialTolerance<Real, Cfg>(coeffs))
            continue;
        Vector3<Real> const aStart = a0 + va0 * t;
        Vector3<Real> const aEnd = a1 + va1 * t;
        Vector3<Real> const bStart = b0 + vb0 * t;
        Vector3<Real> const bEnd = b1 + vb1 * t;
        if (detail::edgesIntersect3D<Real, Cfg>(aStart, aEnd, bStart, bEnd)) {
            toi = t;
            return true;
        }
    }

    return false;
}
} // namespace krd::ipc
