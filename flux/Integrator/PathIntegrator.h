#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

#include "flux/Core/Ray.h"
#include "flux/Sampling/LightSampler.h"
#include "flux/Sampling/Sampler.h"
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

/// \brief Mutable state carried by one path sample.
struct PathState {
    /// Ray for the next radiance traversal.
    Ray ray;
    /// Sampling sequence advanced across path vertices.
    Sampler::Impl sampler;
    /// Radiance accumulated by this sample.
    Spectrum radiance{};
    /// Throughput from the camera to the current vertex.
    Spectrum throughput{1.0F, 1.0F, 1.0F};
    /// Direct-light candidate awaiting a visibility trace.
    PendingShadowQuery pendingShadowQuery;
    /// Number of surface bounces already processed.
    std::uint32_t bounce{};
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
        /// \brief Terminates a path that escaped the scene.
        KIRA_HOST_DEVICE void onMiss(PathState &state) const noexcept { state.active = false; }

        /// \brief Terminates a surface path that has no scattering model.
        KIRA_HOST_DEVICE void
        onSurfaceHit(PathState &state, SurfaceInteraction const &surface) const noexcept {
            (void)surface;
            state.active = false;
        }

        /// \brief Samples one direct-light candidate and terminates the path.
        ///
        /// Light selection and light sampling consume one 1D and one 2D sample
        /// in that order. Visibility remains pending when this function
        /// returns.
        template <typename Scene, typename BSDFType>
        KIRA_HOST_DEVICE void onSurfaceHit(
            PathState &state, Scene const &scene, BSDFType const &bsdf,
            SurfaceInteraction const &surface, Vec3f const &wo
        ) const noexcept {
            state.active = false;

            auto const selectionSample = state.sampler.get1D();
            auto const lightSampleValue = state.sampler.get2D();
            auto const &lightSampler = scene.getLightSampler();
            auto const selected = lightSampler.sample(surface, selectionSample);
            if (!(selected.pmf > 0.0F))
                return;

            auto const lightSample =
                lightSampler.sampleDirect(selected.lightIndex, surface, lightSampleValue);
            if (!(lightSample.pdf > 0.0F))
                return;

            auto const query = BSDFQuery{.surface = surface, .wo = wo};
            auto const f = bsdf.evaluate(query, lightSample.wi);
            auto const cosine = std::max(lightSample.wi.dot(surface.shadingNormal), 0.0F);
            if (!(cosine > 0.0F))
                return;

            auto const pdf = selected.pmf * lightSample.pdf;
            state.pendingShadowQuery = {
                .ray = surface.spawnRayTo(surface.position + lightSample.wi * lightSample.distance),
                .contribution = state.throughput * f * lightSample.radiance * (cosine / pdf),
            };
            state.hasPendingShadowQuery = true;
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
            state.pendingShadowQuery = PendingShadowQuery{};
            state.hasPendingShadowQuery = false;
        }
    };

private:
    PathIntegrator(TXContext &tx, kira::Properties const &props);

    /// \copydoc ContextObject::registerTo
    void registerTo(TXContext &tx) override;
};

static_assert(std::is_standard_layout_v<PendingShadowQuery>);
static_assert(std::is_trivially_copyable_v<PendingShadowQuery>);
static_assert(std::is_standard_layout_v<PathState>);
static_assert(std::is_trivially_copyable_v<PathState>);
} // namespace flux
