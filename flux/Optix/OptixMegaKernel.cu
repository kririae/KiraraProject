#include <optix_device.h>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixContext.cuh"
#include "flux/Optix/OptixImageTexture.cuh"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/CameraImpl.h"
#include "flux/Scene/FilmImpl.h"
#include "flux/Scene/TriangleMeshImpl.h"
#include "flux/Shading/BSDFImpl.h"
#include "flux/Shading/Frame.h"

extern "C" {
__constant__ flux::OptixLaunchParams optixLaunchParams{};
}

namespace {
struct OptixImageTextureEvaluator {
    [[nodiscard]] KIRA_DEVICE static flux::Vec4f eval4f(
        std::uint32_t index, flux::Vec2f uv, flux::Vec2f const &duvdx, flux::Vec2f const &duvdy
    ) noexcept {
        auto const &texture = optixLaunchParams.scene.imageTextures.getTexture(index);
        return texture.apply(texture.sample(uv, duvdx, duvdy));
    }
};

/// \brief What one path vertex asks of the shadow ray.
///
/// A zero \c mask traces without visiting any instance, so a lane with no light candidate holds
/// the traversal site without contributing anything. Such a lane leaves \c ray zeroed, which
/// keeps it out of the continuation frame.
struct ShadowQuery {
    flux::Ray ray;
    flux::Spectrum contribution;
    unsigned int mask;
};

/// \brief Claims the next chunk of \p chunkSize paths for a lane whose chunk is done, and returns
/// its first path, or the path count if no chunk is left.
[[nodiscard]] KIRA_DEVICE std::uint32_t claimChunk(std::uint32_t chunkSize) noexcept {
    auto const launchSize = optixGetLaunchDimensions().x;
    auto const claimed = atomicAdd(optixLaunchParams.nextChunk, 1U);
    return optixLaunchParams.getChunkBegin(claimed, launchSize, chunkSize);
}

/// \brief Builds the camera-ray state of path \p index.
[[nodiscard]] KIRA_DEVICE __forceinline__ flux::PathState startPath(std::uint32_t index) noexcept {
    auto const sample = optixLaunchParams.getLaunchSample(index);
    auto const resolution =
        flux::Vec2u{optixLaunchParams.film.width, optixLaunchParams.film.height};
    auto sampler = optixLaunchParams.sampler;
    sampler.startPixelSample(sample.pixel, sample.sampleIndex, resolution);
    auto const pixelSample = sampler.get2D(flux::SampleUse::Pixel, 0);
    auto const rasterPosition = flux::Vec2f{
        static_cast<float>(sample.pixel.x()) + pixelSample.x(),
        static_cast<float>(sample.pixel.y()) + pixelSample.y(),
    };
    auto const ray = optixLaunchParams.camera.generateRay(
        rasterPosition, sampler.get2D(flux::SampleUse::Lens, 0), resolution
    );
    return flux::PathState{
        .rayDirection = ray.direction,
        .sampler = sampler,
        // The camera vertex has no surface, so the derived ray keeps this origin unoffset.
        .prevLightCtx = {.position = ray.origin},
        .footprint = flux::PackedRayFootprint::pack(
            optixLaunchParams.camera.getRayFootprint(ray.direction, resolution)
        ),
    };
}

/// \brief Shades the vertex \p hit of path \p index and returns its shadow request.
///
/// Reaches no traversal site, so an early return here cannot desynchronize a warp: the caller
/// traces the shadow ray either way.
[[nodiscard]] KIRA_DEVICE __forceinline__ ShadowQuery shade(
    flux::PathState &state, flux::OptixContext::Impl::Hit const &hit, std::uint32_t index
) noexcept {
    auto const writesColor = optixLaunchParams.film.hasChannel<flux::ColorChannel>();
    auto const sampleWeight = optixLaunchParams.getSampleWeight();
    auto const &isect = hit.surface;
    // NVCC generates a faster path when the unpacked footprint stays in this scope.
    auto footprint = state.footprint.unpack();
    footprint.propagate(optixHitObjectGetRayTmax());
    auto const &primitive = optixLaunchParams.scene.table.getPrimitive(isect.primitiveIndex);
    auto const isPrimary = state.depth == 0;
    auto const wo = -state.rayDirection;
    if (writesColor)
        optixLaunchParams.integrator.onEmitterHit(
            state, optixLaunchParams.scene, primitive, isect, wo
        );
    if (isPrimary) {
        optixLaunchParams.film.accumulate<flux::NormalChannel>(
            optixLaunchParams.getLaunchSample(index).pixel, isect.shadingNormal * sampleWeight
        );
    }
    if (!primitive.hasBSDF()) {
        optixLaunchParams.integrator.onSurfaceHit(state);
        return ShadowQuery{};
    }

    auto const writesAlbedo = isPrimary && optixLaunchParams.film.hasChannel<flux::AlbedoChannel>();
    auto const continues = writesColor && optixLaunchParams.integrator.canContinue(state);
    if (!continues && !writesAlbedo) {
        optixLaunchParams.integrator.onSurfaceHit(state);
        return ShadowQuery{};
    }

    auto directLight = flux::DirectLightSample{};
    if (continues) {
        directLight = optixLaunchParams.integrator.sampleDirectLight<OptixImageTextureEvaluator>(
            state, optixLaunchParams.scene,
            {
                .position = isect.position,
                .normal = isect.geometricNormal,
            },
            optixLaunchParams.hasEnvMap
        );
    }

    auto const frame = flux::Frame{isect.shadingNormal};
    auto const localWo = frame.toLocal(wo);
    auto const localLightWi = frame.toLocal(directLight.wi);
    // Split the sample before the BSDF dispatch. NVCC generates a much slower launch when the
    // direction is built in the dispatched call's arguments.
    auto const u = state.sampler.get3D(flux::SampleUse::Bsdf, state.depth);
    auto const uLobe = u.x();
    auto const uDirection = flux::Vec2f{u.y(), u.z()};
    auto const &bsdf = optixLaunchParams.scene.table.getBSDF(primitive.getBSDFIndex());
    auto const texCtx = flux::makeTextureEvalContext(isect, state.rayDirection, footprint);
    auto const result = optixLaunchParams.bsdfDispatcher.execute<OptixImageTextureEvaluator>(
        bsdf, texCtx, localWo, localLightWi, directLight.pdf > 0.0F, uLobe, uDirection
    );

    if (writesAlbedo) {
        optixLaunchParams.film.accumulate<flux::AlbedoChannel>(
            optixLaunchParams.getLaunchSample(index).pixel, result.sample.weight * sampleWeight
        );
    }
    if (!continues) {
        optixLaunchParams.integrator.onSurfaceHit(state);
        return ShadowQuery{};
    }

    auto const candidate = optixLaunchParams.integrator.makeDirectLightCandidate(
        state.throughput, directLight, result.evaluation
    );
    optixLaunchParams.integrator.onSurfaceHit(
        state, isect, frame.toWorld(result.sample.wi), result.sample
    );
    if (state.active)
        optixLaunchParams.integrator.updateFootprint(state, result.sample, footprint);

    if (!candidate.valid)
        return ShadowQuery{};
    return ShadowQuery{
        .ray = directLight.type == flux::LightType::EnvMap ? isect.spawnRay(directLight.wi)
                                                           : isect.spawnRayTo(directLight.position),
        .contribution = candidate.contribution,
        .mask = 255U,
    };
}

/// \brief Advances \p state by one path vertex.
///
/// Owns both traversal sites and reaches them in the same order on every lane, so a warp
/// suspends and resumes together no matter which path each lane carries.
KIRA_DEVICE __forceinline__ void step(flux::PathState &state, std::uint32_t index) noexcept {
    auto query = ShadowQuery{};
    flux::OptixContext::Impl::Hit hit;
    // Reorder on every hit, primary ones included. Guarding this on depth would give the shading
    // two static predecessors, one per continuation, so the compiler would emit a second copy of
    // it, and the guard would stop being warp-uniform once lanes claim new chunks.
    if (optixLaunchParams.scene.intersect(state.getRay(), hit, optixLaunchParams.shaderReorder)) {
        query = shade(state, hit, index);
    } else if (
        optixLaunchParams.film.hasChannel<flux::ColorChannel>() && optixLaunchParams.hasEnvMap
    ) {
        optixLaunchParams.integrator.onMiss<OptixImageTextureEvaluator>(
            state, optixLaunchParams.scene
        );
    } else {
        optixLaunchParams.integrator.onMiss(state);
    }

    // The one shadow traversal, kept outside shade so no early return there can skip it. A lane
    // that skipped it would suspend at the next extension traversal instead and resume in a
    // different continuation from the rest of its warp, with nothing to merge the two groups
    // again.
    auto const visible = optixLaunchParams.scene.isVisible(query.ray, query.mask);
    if (query.mask != 0U && visible)
        state.radiance = state.radiance + query.contribution;
}
} // namespace

extern "C" __global__ void __miss__megakernel() {} // NOLINT

extern "C" __global__ void __raygen__megakernel() { // NOLINT
    auto const chunkSize = std::min(optixLaunchParams.chunkSize, optixLaunchParams.batchSize);
    auto index = optixGetLaunchIndex().x * chunkSize;
    while (index < optixLaunchParams.pathCount) {
        auto state = startPath(index);
        do
            step(state, index);
        while (state.active);
        optixLaunchParams.film.accumulate<flux::ColorChannel>(
            optixLaunchParams.getLaunchSample(index).pixel,
            state.radiance * optixLaunchParams.getSampleWeight()
        );

        if (++index % chunkSize == 0)
            index = claimChunk(chunkSize);
    }
}
