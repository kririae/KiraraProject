#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "flux/Core/Ray.h"
#include "flux/Sampling/LightSampler.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Candidate contribution awaiting a visibility test.
struct DirectLightCandidate {
    Ray visibilityRay;
    Spectrum contribution{};
    bool valid{};
};

/// \brief State carried by one path between radiance traversals.
///
/// \verbatim
/// previous vertex -- ray --> current hit
///                    \---- previous light context and BSDF PDF
/// \endverbatim
///
/// A BSDF sample replaces \c ray and records the data needed when that ray
/// reaches an emitter.
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

public:
    /// Previous vertex used to evaluate the light-sampling PDF at an emitter.
    LightSamplingContext previousLightContext{};
    /// BSDF PDF that produced \c ray.
    float previousBSDFPdf{};
    /// Number of surface interactions that produced a continuation ray.
    std::uint32_t depth{};
    /// Whether another radiance vertex should be processed.
    bool active{true};
    /// Whether the BSDF sample that produced \c ray was discrete.
    bool previousDelta{};
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
        /// in that order. A valid result contains its visibility ray and candidate contribution.
        template <typename BackendContext, typename BSDFImpl>
        [[nodiscard]] KIRA_HOST_DEVICE DirectLightCandidate sampleDirectLighting(
            PathState &state, BackendContext const &backend, BSDFImpl const &bsdf,
            SurfaceInteraction const &surface, Vec3f const &wo
        ) const noexcept {
            if (state.depth + 1 >= maxDepth)
                return {};

            auto const ctx = LightSamplingContext{
                .position = surface.position,
                .normal = surface.shadingNormal,
            };
            auto const &lightSampler = backend.getLightSampler();
            auto const uSelect = state.sampler.get1D();
            auto const uLight = state.sampler.get2D();
            auto const selected = lightSampler.sample(ctx, uSelect);
            if (selected.pmf <= 0.0F)
                return {};

            auto const lightSample =
                lightSampler.sampleDirect(backend, selected.lightIndex, ctx, uLight);
            if (lightSample.pdf <= 0.0F)
                return {};

            auto const query = BSDFQuery{.surface = surface, .wo = wo};
            auto const eval = bsdf.evaluateAndPdf(query, lightSample.wi);
            auto const cosTheta = std::abs(lightSample.wi.dot(surface.shadingNormal));
            if (cosTheta == 0.0F || eval.f.norm2() == 0.0F)
                return {};

            auto const lightPdf = selected.pmf * lightSample.pdf;
            auto const mis = lightSample.delta ? 1.0F : misWeight(lightPdf, eval.pdf);
            return {
                .visibilityRay = surface.spawnRayTo(lightSample.position),
                .contribution =
                    state.throughput * eval.f * lightSample.radiance * (cosTheta * mis / lightPdf),
                .valid = true,
            };
        }

        /// \brief Adds emission at the current surface with the BSDF-sampling MIS weight.
        template <typename BackendContext>
        KIRA_HOST_DEVICE void onEmitterHit(
            PathState &state, BackendContext const &backend, Primitive::Impl const &primitive,
            SurfaceInteraction const &surface, Vec3f const &wo
        ) const noexcept {
            if (!primitive.hasEDF())
                return;

            auto weight = 1.0F;
            if (state.depth > 0 && !state.previousDelta) {
                auto const &lightSampler = backend.getLightSampler();
                auto const lightPdf =
                    lightSampler.pmf(state.previousLightContext, primitive.getLightIndex()) *
                    lightSampler.pdfDirect(
                        backend, primitive.getLightIndex(), state.previousLightContext, surface
                    );
                weight = misWeight(state.previousBSDFPdf, lightPdf);
            }
            auto const emission = backend.getEDF(primitive.getEDFIndex())
                                      .evaluate({
                                          .geometricNormal = surface.geometricNormal,
                                          .wo = wo,
                                      });
            state.radiance = state.radiance + state.throughput * emission * weight;
        }

        /// \brief Samples direct lighting and prepares the next radiance ray.
        template <typename BackendContext, typename BSDFImpl>
        [[nodiscard]] KIRA_HOST_DEVICE DirectLightCandidate onSurfaceHit(
            PathState &state, BackendContext const &backend, BSDFImpl const &bsdf,
            SurfaceInteraction const &surface, Vec3f const &wo
        ) const noexcept {
            if (state.depth + 1 >= maxDepth) {
                state.active = false;
                return {};
            }

            auto const directLight = sampleDirectLighting(state, backend, bsdf, surface, wo);
            auto const query = BSDFQuery{.surface = surface, .wo = wo};
            auto const sample = bsdf.sample(query, state.sampler.get1D(), state.sampler.get2D());
            auto const cosTheta = std::abs(sample.wi.dot(surface.shadingNormal));
            if (sample.pdf <= 0.0F || cosTheta == 0.0F || sample.f.norm2() == 0.0F) {
                state.active = false;
                return directLight;
            }

            state.previousLightContext = {
                .position = surface.position,
                .normal = surface.shadingNormal,
            };
            state.previousBSDFPdf = sample.pdf;
            state.previousDelta = sample.isDelta();
            state.ray = surface.spawnRay(sample.wi);
            state.throughput = state.throughput * sample.f * (cosTheta / sample.pdf);
            state.eta *= sample.eta;
            ++state.depth;

            if (state.throughput.hmax() == 0.0F) {
                state.active = false;
                return directLight;
            }

            if (state.depth >= rrDepth) {
                // Squared IOR compensates radiance scaling across transmission.
                auto const q = std::min(state.throughput.hmax() * state.eta * state.eta, rrProb);
                if (q <= 0.0F || state.sampler.get1D() >= q) {
                    state.active = false;
                    return directLight;
                }
                state.throughput = state.throughput / q;
            }
            return directLight;
        }
    };

    [[nodiscard]] Impl getImpl() const noexcept { return impl_; }

private:
    PathIntegrator(TXContext &tx, kira::Properties const &props);

    /// \copydoc ContextObject::registerTo
    void registerTo(TXContext &tx) override;

    Impl impl_;
};

static_assert(std::is_standard_layout_v<DirectLightCandidate>);
static_assert(std::is_trivially_copyable_v<DirectLightCandidate>);
static_assert(std::is_standard_layout_v<PathState>);
static_assert(std::is_trivially_copyable_v<PathState>);
static_assert(sizeof(PathState) <= 128, "PathState carry-over grew");
static_assert(std::is_standard_layout_v<PathIntegrator::Impl>);
static_assert(std::is_trivially_copyable_v<PathIntegrator::Impl>);
} // namespace flux
