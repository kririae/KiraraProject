#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>

#include "flux/Core/MathUtils.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixImageTexture.cuh"
#include "flux/Optix/OptixLightSampler.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Sampling/Distribution2D.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/Texture.h"
#include "kira/SmallVector.h"

namespace flux {
namespace {
struct SampleEnvMapWeights {
    OptixImageTextureTable imageTextures;
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
        auto const &texture = imageTextures.getTexture(textureIndex);
        auto const value = texture.apply(texture.samplePoint(uv));
        auto const radiance = Spectrum{value.x(), value.y(), value.z()} * scale;
        auto const theta = std::numbers::pi_v<float> * (static_cast<float>(y) + 0.5F) /
                           static_cast<float>(extent.y());
        weights[index] = std::max(luminance(radiance), 0.0F) * std::sin(theta);
    }
};

} // namespace

void OptixLightSampler::sync(
    Context const &context, OptixImageTextureTable imageTextures, float sceneRadius
) {
    points_.clear();
    primAreaScales_.clear();
    envMap_.clear();
    handles_.clear();
    cdf_.clear();
    primSlots_.clear();

    auto const envMap = context.getActiveEnvMap();
    auto const image = envMap ? envMap->getTexture().dynamicCast<ImageTexture const>() : nullptr;
    std::optional<EnvMapKey> key;
    // The weights read texel centers with point filtering, so the filter and address modes do not
    // change them, and the rotation applies only when rendering. The extent, components, color
    // space, and whether the image is a placeholder belong to the ImageAsset, which is immutable
    // and whose ID never repeats.
    if (image)
        key = EnvMapKey{
            .imageId = image->getImageAsset()->getId(),
            .componentMapping = image->getComponentMapping(),
            .fallback = image->getFallback(),
            .scale = image->getScale(),
            .bias = image->getBias(),
            .envMapScale = envMap->getScale(),
        };
    // Reset before building, so that the old and the new distribution never coexist in memory.
    if (envMapDistribution_ && envMapDistribution_->key != key)
        envMapDistribution_.reset();
    if (key && !envMapDistribution_)
        envMapDistribution_ = buildEnvMapDistribution(*image, *key, imageTextures);

    std::optional<float> envMapPower;
    if (envMap)
        envMapPower = uploadEnvMap(
            *envMap, envMapDistribution_ ? &*envMapDistribution_ : nullptr, sceneRadius
        );

    table_.build(context, envMapPower);
    points_.copyFromHost(table_.lights.points, deletionQueue_);
    primAreaScales_.copyFromHost(table_.lights.primAreaScales, deletionQueue_);
    handles_.copyFromHost(table_.slots.handles, deletionQueue_);
    cdf_.copyFromHost(table_.slots.cdf, deletionQueue_);
    primSlots_.copyFromHost(table_.slots.primSlots, deletionQueue_);
}

OptixLightSampler::EnvMapDistribution OptixLightSampler::buildEnvMapDistribution(
    ImageTexture const &image, EnvMapKey const &key, OptixImageTextureTable imageTextures
) const {
    auto const extent = image.getImageAsset()->getExtent();
    auto const numPixels = static_cast<std::size_t>(extent.x()) * extent.y();
    DeviceBuffer<float> weights(getStream());
    weights.resize(numPixels);
    launchLinearKernel(
        numPixels,
        SampleEnvMapWeights{
            .imageTextures = imageTextures,
            .textureIndex = image.getImpl().storage.image.imageTextureIndex,
            .scale = key.envMapScale,
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

    auto distribution = EnvMapDistribution{
        .key = key,
        .extent = extent,
        .weightSum = buildCDF2D(
            extent, {hostWeights.data(), hostWeights.size()}, {hostRows.data(), hostRows.size()}
        ),
        .cdf = DeviceBuffer<float>(getStream()),
        .rows = DeviceBuffer<float>(getStream()),
    };
    if (distribution.weightSum > 0.0F) {
        distribution.cdf.copyFromHost({hostWeights.data(), hostWeights.size()}, deletionQueue_);
        distribution.rows.copyFromHost({hostRows.data(), hostRows.size()}, deletionQueue_);
    }
    return distribution;
}

float OptixLightSampler::uploadEnvMap(
    EnvMapLight const &envMap, EnvMapDistribution const *distribution, float sceneRadius
) {
    auto luminanceIntegral = 0.0F;
    auto view = Distribution2D{};
    if (distribution) {
        auto const numPixels =
            static_cast<std::size_t>(distribution->extent.x()) * distribution->extent.y();
        luminanceIntegral = distribution->weightSum * 2.0F * std::numbers::pi_v<float> *
                            std::numbers::pi_v<float> / static_cast<float>(numPixels);
        if (distribution->weightSum > 0.0F)
            view = {
                .condCDF = distribution->cdf.data(),
                .rowCDF = distribution->rows.data(),
                .extent = distribution->extent,
            };
    } else {
        auto const texture = envMap.getTexture()->getImpl();
        luminanceIntegral =
            4.0F * std::numbers::pi_v<float> *
            std::max(luminance(texture.storage.constant.value * envMap.getScale()), 0.0F);
    }

    auto const impl = envMap.getImpl(view);
    envMap_.copyFromHost({&impl, 1}, deletionQueue_);
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
        .envMapSlot = table_.slots.envMapSlot,
        .power = {
            .cdf = cdf_.data(),
            .sum = table_.slots.cdf.empty() ? 0.0F : table_.slots.cdf.back(),
            .numLights = static_cast<std::uint32_t>(handles_.size()),
        },
    };
}
} // namespace flux
