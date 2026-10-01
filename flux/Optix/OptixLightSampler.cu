#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>

#include "flux/Core/MathUtils.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixImageTexturePool.cuh"
#include "flux/Optix/OptixLightSampler.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Sampling/Distribution2D.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/Texture.h"
#include "kira/Anyhow.h"
#include "kira/SmallVector.h"

namespace flux {
namespace {
struct SampleEnvMapWeights {
    OptixImageTexturePool::Impl imageTextures;
    std::uint32_t textureIndex;
    Spectrum scale;
    float *weights;
    Vec2u extent;

    KIRA_DEVICE void operator()(std::size_t index) const noexcept {
        auto const x = static_cast<std::uint32_t>(index % extent.x());
        auto const y = static_cast<std::uint32_t>(index / extent.x());
        auto const uv = Vec2f{
            (static_cast<float>(x) + 0.5F) / static_cast<float>(extent.x()),
            1.0F - (static_cast<float>(y) + 0.5F) / static_cast<float>(extent.y()),
        };
        auto const &texture = imageTextures.get(textureIndex);
        auto const value = texture.componentMapping.apply(texture.samplePoint(uv));
        auto const radiance = Spectrum{value.x(), value.y(), value.z()} * scale;
        auto const theta = std::numbers::pi_v<float> * (static_cast<float>(y) + 0.5F) /
                           static_cast<float>(extent.y());
        weights[index] = std::max(luminance(radiance), 0.0F) * std::sin(theta);
    }
};
} // namespace

void OptixLightSampler::build(
    Context const &context, OptixImageTexturePool::Impl imageTextures, float sceneRadius
) {
    points_.clear();
    primAreaScales_.clear();
    envMap_.clear();
    envMapCDF_.clear();
    envMapRows_.clear();
    handles_.clear();
    cdf_.clear();
    primSlots_.clear();

    auto const envMap = context.getActiveEnvMap();
    std::optional<float> envMapPower;
    if (envMap)
        envMapPower = buildEnvMap(*envMap, imageTextures, sceneRadius);

    staging_.build(context, envMapPower);
    points_.copyFromHost(staging_.lights.points);
    primAreaScales_.copyFromHost(staging_.lights.primAreaScales);
    handles_.copyFromHost(staging_.slots.handles);
    cdf_.copyFromHost(staging_.slots.cdf);
    primSlots_.copyFromHost(staging_.slots.primSlots);
}

float OptixLightSampler::buildEnvMap(
    EnvMapLight const &envMap, OptixImageTexturePool::Impl imageTextures, float sceneRadius
) {
    auto const texture = envMap.getTexture()->getImpl();
    auto luminanceIntegral = 0.0F;
    auto distribution = Distribution2D{};
    if (texture.type == TextureType::Constant) {
        luminanceIntegral =
            4.0F * std::numbers::pi_v<float> *
            std::max(luminance(texture.storage.constant.value * envMap.getScale()), 0.0F);
    } else if (texture.type == TextureType::Image) {
        auto image = envMap.getTexture().dynamicCast<ImageTexture const>();
        if (!image)
            throw kira::Anyhow("OptixLightSampler: image texture implementation mismatch");
        auto const extent = image->getImageAsset()->getExtent();
        auto const numPixels = static_cast<std::size_t>(extent.x()) * extent.y();
        DeviceBuffer<float> weights(getStream());
        weights.resize(numPixels);
        launchLinearKernel(
            numPixels,
            SampleEnvMapWeights{
                .imageTextures = imageTextures,
                .textureIndex = texture.storage.image.imageTextureIndex,
                .scale = envMap.getScale(),
                .weights = weights.data(),
                .extent = extent,
            },
            getStream()
        );

        kira::SmallVector<float, 0> hostWeights;
        kira::SmallVector<float, 0> hostRows;
        hostWeights.resize_for_overwrite(numPixels);
        hostRows.resize_for_overwrite(extent.y());
        weights.copyToHost({hostWeights.data(), hostWeights.size()});
        cudaCheck(cudaStreamSynchronize(getStream()));
        auto const weightSum = buildCDF2D(
            extent, {hostWeights.data(), hostWeights.size()}, {hostRows.data(), hostRows.size()}
        );
        luminanceIntegral = weightSum * 2.0F * std::numbers::pi_v<float> *
                            std::numbers::pi_v<float> / static_cast<float>(numPixels);
        if (weightSum > 0.0F) {
            envMapCDF_.copyFromHost({hostWeights.data(), hostWeights.size()});
            envMapRows_.copyFromHost({hostRows.data(), hostRows.size()});
            distribution = {
                .condCDF = envMapCDF_.data(),
                .rowCDF = envMapRows_.data(),
                .extent = extent,
            };
        }
    }

    auto const impl = envMap.getImpl(distribution);
    envMap_.copyFromHost({&impl, 1});
    return envMap.estimatePower(sceneRadius, luminanceIntegral);
}

OptixLightSampler::Impl OptixLightSampler::getImpl() const noexcept {
    return {
        .table =
            {
                .points = points_.data(),
                .primAreaScales = primAreaScales_.data(),
                .envMap = envMap_.data(),
            },
        .handles = handles_.data(),
        .primSlots = primSlots_.data(),
        .envMapSlot = staging_.slots.envMapSlot,
        .power = {
            .cdf = cdf_.data(),
            .sum = staging_.slots.cdf.empty() ? 0.0F : staging_.slots.cdf.back(),
            .numLights = static_cast<std::uint32_t>(handles_.size()),
        },
    };
}
} // namespace flux
