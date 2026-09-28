#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

#include "flux/Core/Ray.h"
#include "flux/Core/RayFootprint.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/LightSamplingImpl.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Candidate contribution awaiting a visibility test.
struct DirectLightCandidate {
    Spectrum contribution{};
    bool valid{};
};

/// \brief State carried by one path between radiance traversals.
///
/// \verbatim
/// previous vertex -- rayDirection --> current hit
///                    \---- previous light context and BSDF PDF
/// \endverbatim
///
/// A BSDF sample replaces \c rayDirection and records the data needed when the
/// ray it starts reaches an emitter.
struct PathState {
    /// Direction the last vertex scattered into, which the next radiance traversal follows.
    Vec3f rayDirection;
    /// Sampling sequence advanced across path vertices.
    Sampler::Impl sampler;
    /// Radiance accumulated by this sample.
    Spectrum radiance{};
    /// Path throughput carried by the next ray.
    Spectrum throughput{1.0F, 1.0F, 1.0F};
    /// Product of relative IORs along the path.
    float eta{1.0F};

public:
    /// Previous vertex used to evaluate the light-sampling PDF at an emitter.
    LightSamplingContext prevLightCtx{};
    /// BSDF PDF that produced \c rayDirection.
    float prevBSDFPdf{};
    PackedRayFootprint footprint{};
    /// Number of surface interactions that produced a continuation ray.
    std::uint32_t depth{};
    /// Whether another radiance vertex should be processed.
    bool active{true};
    /// Whether the BSDF sample that produced \c rayDirection was discrete.
    bool prevDelta{};

public:
    /// \brief Returns the ray for the next radiance traversal.
    ///
    /// The ray starts at \c prevLightCtx.position, offset along \c prevLightCtx.normal as
    /// \c SurfaceInteraction::spawnRay offsets it. A zero normal, as at the camera, leaves the
    /// origin unoffset.
    [[nodiscard]] KIRA_HOST_DEVICE Ray getRay() const noexcept {
        return {
            .origin = offsetRayOrigin(prevLightCtx.position, prevLightCtx.normal, rayDirection),
            .direction = rayDirection,
        };
    }
};

/// \brief Estimates radiance with path tracing.
///
/// The first path integrator added to a context becomes active after its
/// transaction succeeds.
///
/// \par Properties
/// - \c max_depth: maximum path depth; defaults to 8.
/// - \c rr_depth: first depth subject to Russian roulette; defaults to 2.
/// - \c rr_prob: maximum Russian roulette continuation probability; defaults to 0.95.
/// - \c shader_reorder: requests OptiX shader execution reordering; defaults to false, since
///   claiming paths already keeps warps full and reordering then only adds its own cost.
class PathIntegrator final : public RenderObject {
    friend class TXContext;

public:
    /// \brief Advances one path between traversal calls.
    ///
    /// Renderer backends own traversal and scheduling. This type handles the
    /// transport steps shared by those backends.
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

        /// \brief Adds environment emission and terminates an escaped path.
        template <typename Evaluator, typename BackendContext>
        KIRA_HOST_DEVICE void
        onMiss(PathState &state, BackendContext const &backend) const noexcept {
            auto const *envMap = backend.lightSampler.table.envMap;
            if (!envMap) {
                state.active = false;
                return;
            }

            auto radiance = Spectrum{};
            auto weight = 1.0F;
            auto const coneAngle = state.footprint.unpack().angle();
            if (state.depth > 0 && !state.prevDelta) {
                float envMapPdf;
                radiance = envMap->template evalAndPdf<Evaluator>(
                    state.rayDirection, coneAngle, envMapPdf
                );
                auto const selectPmf = backend.lightSampler.pmf(
                    state.prevLightCtx, {.type = LightType::EnvMap, .index = 0}
                );
                weight = misWeight(state.prevBSDFPdf, selectPmf * envMapPdf);
            } else
                radiance = envMap->template eval<Evaluator>(state.rayDirection, coneAngle);

            state.radiance = state.radiance + state.throughput * radiance * weight;
            state.active = false;
        }

        [[nodiscard]] KIRA_HOST_DEVICE bool canContinue(PathState const &state) const noexcept {
            return state.depth + 1 < maxDepth;
        }

        /// \brief Terminates a surface path that has no scattering model.
        KIRA_HOST_DEVICE void onSurfaceHit(PathState &state) const noexcept {
            state.active = false;
        }

        /// \brief Samples one light for next-event estimation.
        ///
        /// Light selection and the position on the light come from one 3D sample. The
        /// returned PDF includes light selection.
        template <typename Evaluator, typename BackendContext>
        [[nodiscard]] KIRA_HOST_DEVICE DirectLightSample sampleDirectLight(
            PathState &state, BackendContext const &backend, LightSamplingContext const &ctx,
            bool hasEnvMap
        ) const noexcept {
            auto const u = state.sampler.get3D(SampleUse::Light, state.depth);
            auto const uSelect = u.x();
            auto const uLight = Vec2f{u.y(), u.z()};
            return flux::sampleDirectLight<Evaluator>(backend, ctx, uSelect, uLight, hasEnvMap);
        }

        /// \brief Builds a direct-light contribution awaiting a visibility test.
        ///
        /// A valid candidate has a positive light PDF and a nonzero BSDF value.
        [[nodiscard]] KIRA_HOST_DEVICE KIRA_FORCEINLINE DirectLightCandidate
        makeDirectLightCandidate(
            Spectrum const &throughput, DirectLightSample const &lightSample,
            BSDFEvaluation const &evaluation
        ) const noexcept {
            if (lightSample.pdf <= 0.0F || evaluation.value.norm2() == 0.0F)
                return {};

            auto mis = 1.0F;
            if (!lightSample.delta)
                mis = misWeight(lightSample.pdf, evaluation.pdf);
            return {
                .contribution =
                    throughput * evaluation.value * lightSample.radiance * (mis / lightSample.pdf),
                .valid = true,
            };
        }

        /// \brief Adds emission at the current surface with the BSDF-sampling MIS weight.
        template <typename BackendContext>
        KIRA_HOST_DEVICE void onEmitterHit(
            PathState &state, BackendContext const &backend, Primitive::Impl const &prim,
            SurfaceInteraction const &isect, Vec3f const &wo
        ) const noexcept {
            if (!prim.hasEDF())
                return;

            auto weight = 1.0F;
            if (state.depth > 0 && !state.prevDelta) {
                auto const lightPdf = backend.pdfDirectLight(state.prevLightCtx, prim, isect);
                weight = misWeight(state.prevBSDFPdf, lightPdf);
            }

            auto const emission = backend.getEDF(prim.getEDFIndex())
                                      .evaluate({
                                          .geometricNormal = isect.geometricNormal,
                                          .wo = wo,
                                      });
            state.radiance = state.radiance + state.throughput * emission * weight;
        }

        /// \brief Applies a BSDF sample at the current surface.
        ///
        /// \param wi World-space direction toward the next path vertex.
        KIRA_HOST_DEVICE void onSurfaceHit(
            PathState &state, SurfaceInteraction const &isect, Vec3f const &wi,
            BSDFSample const &sample
        ) const noexcept {
            if (sample.pdf <= 0.0F || sample.weight.norm2() == 0.0F) {
                state.active = false;
                return;
            }

            state.prevLightCtx = {
                .position = isect.position,
                .normal = isect.geometricNormal,
            };
            state.prevBSDFPdf = sample.pdf;
            state.prevDelta = sample.isDelta();
            state.rayDirection = wi;
            state.throughput = state.throughput * sample.weight;
            state.eta *= sample.eta;
            ++state.depth;

            if (state.throughput.hmax() == 0.0F) {
                state.active = false;
                return;
            }

            if (state.depth >= rrDepth) {
                // Squared IOR compensates radiance scaling across transmission.
                auto const q = std::min(state.throughput.hmax() * state.eta * state.eta, rrProb);
                if (q <= 0.0F || state.sampler.get1D(SampleUse::Terminate, state.depth) >= q) {
                    state.active = false;
                    return;
                }
                state.throughput = state.throughput / q;
            }
        }

        KIRA_HOST_DEVICE void updateFootprint(
            PathState &state, BSDFSample const &sample, RayFootprint footprint
        ) const noexcept {
            if (sample.lobe == BSDFLobe::DiffuseReflection ||
                sample.lobe == BSDFLobe::DiffuseTransmission) {
                footprint.setDiffuse();
                state.footprint = PackedRayFootprint::pack(footprint);
                return;
            }

            if (sample.lobe == BSDFLobe::GlossyTransmission ||
                sample.lobe == BSDFLobe::DeltaTransmission)
                footprint.refract(sample.eta);

            // Continuous sample weight is f * abs(cos theta) / pdf.
            if (!sample.isDelta())
                footprint.scatter(sample.weight.hmax() * sample.pdf / std::abs(sample.wi.z()));

            state.footprint = PackedRayFootprint::pack(footprint);
        }
    };

    [[nodiscard]] Impl getImpl() const noexcept { return impl_; }

    /// \brief Returns whether OptiX radiance traversal requests shader execution reordering.
    [[nodiscard]] bool usesShaderReorder() const noexcept { return shaderReorder_; }

private:
    PathIntegrator(TXContext &tx, kira::Properties const &props);

    /// \copydoc ContextObject::registerTo
    void registerTo(TXContext &tx) override;

    Impl impl_;
    bool shaderReorder_;
};

static_assert(std::is_standard_layout_v<DirectLightCandidate>);
static_assert(std::is_trivially_copyable_v<DirectLightCandidate>);
static_assert(std::is_standard_layout_v<PathState>);
static_assert(std::is_trivially_copyable_v<PathState>);
static_assert(sizeof(PathState) <= 112, "PathState carry-over grew");
static_assert(std::is_standard_layout_v<PathIntegrator::Impl>);
static_assert(std::is_trivially_copyable_v<PathIntegrator::Impl>);
} // namespace flux
