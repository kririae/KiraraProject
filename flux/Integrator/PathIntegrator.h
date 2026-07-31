#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "flux/Core/Ray.h"
#include "flux/Sampling/LightSampler.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Deferred visibility test for one direct-light candidate.
///
/// \c onSurfaceHit stores this value after selecting a light and evaluating
/// the BSDF. The backend resolves visibility after the radiance traversal and
/// accepts \c contribution only when the ray is visible.
struct PendingShadowQuery {
    /// Visibility ray from the surface to the sampled light point.
    Ray ray;
    /// Candidate radiance contribution before visibility.
    Spectrum contribution{};
};

/// \brief State carried by one path between radiance traversals.
///
/// \verbatim
/// input:             ray --> hit
/// continuation:      hit -- ray --> next hit
/// traversal result:  hit -- pendingShadowQuery --> sampled light
/// \endverbatim
///
/// A valid BSDF sample replaces \c ray with the continuation.
/// The scheduler resolves the shadow query before the next radiance traversal.
struct PathState {
    /// Ray for the next radiance traversal.
    Ray ray;
    /// Sampling sequence advanced across path vertices.
    Sampler::Impl sampler;
    /// Radiance accumulated by this sample.
    Spectrum radiance{};
    /// Path throughput carried by \c ray.
    Spectrum throughput{1.0F, 1.0F, 1.0F};
    /// Product of relative IORs along the path.
    float eta{1.0F};
    /// Number of surface interactions that produced a continuation ray.
    std::uint32_t depth{};

    // Traversal results consumed by the scheduler. An inactive path may still
    // carry a direct-light candidate.
public:
    /// Direct-light candidate awaiting a visibility trace.
    PendingShadowQuery pendingShadowQuery;
    /// Whether another radiance vertex should be processed.
    bool active{true};
    /// Whether \c pendingShadowQuery awaits a visibility trace.
    bool hasPendingShadowQuery{};
};

/// \brief Selects path tracing as a context's transport algorithm.
///
/// The first path integrator added to a context becomes active after its
/// transaction succeeds.
class PathIntegrator final : public RenderObject {
    friend class TXContext;

public:
    /// \brief Operations that advance one path.
    ///
    /// Renderer backends own traversal and scheduling. This implementation
    /// owns the transport transitions shared by those schedulers.
    struct Impl {
        /// Maximum number of path vertices.
        ///
        /// A value of one only evaluates directly visible emission.
        std::uint32_t maxDepth;
        /// RR applies at and after this path depth.
        std::uint32_t rrDepth;
        /// Maximum RR continuation probability.
        float rrProb;

    public:
        Impl() = default;

        KIRA_HOST_DEVICE
        Impl(std::uint32_t maxDepth, std::uint32_t rrDepth, float rrProb) noexcept
            : maxDepth(maxDepth), rrDepth(rrDepth), rrProb(rrProb) {}

        /// \brief Returns the power-heuristic weight for the first PDF.
        [[nodiscard]] KIRA_HOST_DEVICE static float
        misWeight(float firstPdf, float secondPdf) noexcept {
            firstPdf *= firstPdf;
            secondPdf *= secondPdf;
            return firstPdf / (firstPdf + secondPdf);
        }

        /// \brief Terminates a path that escaped the scene.
        KIRA_HOST_DEVICE void onMiss(PathState &state) const noexcept { state.active = false; }

        /// \brief Terminates a surface path that has no scattering model.
        KIRA_HOST_DEVICE void
        onSurfaceHit(PathState &state, SurfaceInteraction const &surface) const noexcept {
            (void)surface;
            state.active = false;
        }

        /// \brief Samples one direct-light candidate.
        ///
        /// Light selection and light sampling consume one 1D and one 2D sample
        /// in that order. Visibility remains pending when this function
        /// returns.
        template <typename Scene, typename BSDFType>
        KIRA_HOST_DEVICE void sampleDirectLighting(
            PathState &state, Scene const &scene, BSDFType const &bsdf,
            SurfaceInteraction const &surface, Vec3f const &wo
        ) const noexcept {
            auto const ctx = LightSamplingContext{
                .position = surface.position,
                .normal = surface.shadingNormal,
            };
            auto const &lightSampler = scene.getLightSampler();
            auto const uSelect = state.sampler.get1D();
            auto const uLight = state.sampler.get2D();
            auto const selected = lightSampler.sample(ctx, uSelect);
            if (selected.pmf <= 0.0F)
                return;

            auto const lightSample = lightSampler.sampleDirect(selected.lightIndex, ctx, uLight);
            if (lightSample.pdf <= 0.0F)
                return;

            auto const query = BSDFQuery{.surface = surface, .wo = wo};
            auto const eval = bsdf.evaluateAndPdf(query, lightSample.wi);
            auto const cosTheta = std::abs(lightSample.wi.dot(surface.shadingNormal));
            if (cosTheta == 0.0F || eval.f.norm2() == 0.0F)
                return;

            auto const lightPdf = selected.pmf * lightSample.pdf;
            auto const mis = lightSample.delta ? 1.0F : misWeight(lightPdf, eval.pdf);
            state.pendingShadowQuery = {
                .ray = surface.spawnRayTo(surface.position + lightSample.wi * lightSample.distance),
                .contribution =
                    state.throughput * eval.f * lightSample.radiance * (cosTheta * mis / lightPdf),
            };
            state.hasPendingShadowQuery = true;
        }

        /// \brief Evaluates one surface and prepares the next radiance ray.
        template <typename Scene, typename BSDFType>
        KIRA_HOST_DEVICE void onSurfaceHit(
            PathState &state, Scene const &scene, BSDFType const &bsdf,
            SurfaceInteraction const &surface, Vec3f const &wo
        ) const noexcept {
            if (state.depth + 1 >= maxDepth) {
                state.active = false;
                return;
            }

            sampleDirectLighting(state, scene, bsdf, surface, wo);

            auto const query = BSDFQuery{.surface = surface, .wo = wo};
            auto const sample = bsdf.sample(query, state.sampler.get1D(), state.sampler.get2D());
            auto const cosTheta = std::abs(sample.wi.dot(surface.shadingNormal));
            if (sample.pdf <= 0.0F || cosTheta == 0.0F || sample.f.norm2() == 0.0F) {
                state.active = false;
                return;
            }

            state.ray = surface.spawnRay(sample.wi);
            state.throughput = state.throughput * sample.f * (cosTheta / sample.pdf);
            state.eta *= sample.eta;
            ++state.depth;

            if (state.throughput.hmax() == 0.0F) {
                state.active = false;
                return;
            }

            if (state.depth >= rrDepth) {
                // Squared IOR compensates radiance scaling across transmission.
                auto const q = std::min(state.throughput.hmax() * state.eta * state.eta, rrProb);
                if (q <= 0.0F || state.sampler.get1D() >= q) {
                    state.active = false;
                    return;
                }
                state.throughput = state.throughput / q;
            }
        }

        /// \brief Resolves a pending direct-light visibility test.
        ///
        /// Does nothing when no test is pending. A visible test adds the
        /// candidate contribution. The test is then cleared.
        KIRA_HOST_DEVICE void
        resolvePendingShadowQuery(PathState &state, bool visible) const noexcept {
            if (!state.hasPendingShadowQuery)
                return;

            if (visible)
                state.radiance = state.radiance + state.pendingShadowQuery.contribution;
            state.hasPendingShadowQuery = false;
        }
    };

    [[nodiscard]] Impl getImpl() const noexcept { return impl_; }

private:
    PathIntegrator(TXContext &tx, kira::Properties const &props);

    /// \copydoc ContextObject::registerTo
    void registerTo(TXContext &tx) override;

    Impl impl_;
};

static_assert(std::is_standard_layout_v<PendingShadowQuery>);
static_assert(std::is_trivially_copyable_v<PendingShadowQuery>);
static_assert(std::is_standard_layout_v<PathState>);
static_assert(std::is_trivially_copyable_v<PathState>);
static_assert(sizeof(PathState) <= 136, "PathState carry-over grew");
static_assert(std::is_standard_layout_v<PathIntegrator::Impl>);
static_assert(std::is_trivially_copyable_v<PathIntegrator::Impl>);
} // namespace flux
