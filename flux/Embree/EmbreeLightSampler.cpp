#include "flux/Embree/EmbreeLightSampler.h"

#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "flux/Core/MathUtils.h"
#include "flux/Sampling/Distribution2D.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/Texture.h"
#include "kira/Anyhow.h"

namespace flux {
void EmbreeLightSampler::build(
    Context const &context, std::span<Primitive::Impl> primImpls,
    EmbreeImageTexturePool::Impl imageTextures, float sceneRadius
) {
    envMapCDF_.clear();
    envMapRows_.clear();
    envMap_.reset();
    auto const envMap = context.getActiveEnvMap();
    std::optional<float> envMapPower;
    if (envMap)
        envMapPower = buildEnvMap(*envMap, imageTextures, sceneRadius);

    tableData_.build(context, primImpls, envMapPower);
    powerCDF_ = buildLightPowerCDF(tableData_.powers);
}

float EmbreeLightSampler::buildEnvMap(
    EnvMapLight const &envMap, EmbreeImageTexturePool::Impl imageTextures, float sceneRadius
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
            throw kira::Anyhow("EmbreeLightSampler: image texture implementation mismatch");
        auto const extent = image->getImageAsset()->getExtent();
        auto const numPixels = static_cast<std::size_t>(extent.x()) * extent.y();
        envMapCDF_.resize_for_overwrite(numPixels);
        envMapRows_.resize_for_overwrite(extent.y());

        tbb::parallel_for(std::size_t{0}, numPixels, [&](std::size_t index) {
            auto const x = static_cast<std::uint32_t>(index % extent.x());
            auto const y = static_cast<std::uint32_t>(index / extent.x());
            auto const uv = Vec2f{
                (static_cast<float>(x) + 0.5F) / static_cast<float>(extent.x()),
                1.0F - (static_cast<float>(y) + 0.5F) / static_cast<float>(extent.y()),
            };
            auto const value =
                imageTextures.evalPoint4f(texture.storage.image.imageTextureIndex, uv);
            auto const radiance = Spectrum{value.x(), value.y(), value.z()} * envMap.getScale();
            auto const theta = std::numbers::pi_v<float> * (static_cast<float>(y) + 0.5F) /
                               static_cast<float>(extent.y());
            envMapCDF_[index] = std::max(luminance(radiance), 0.0F) * std::sin(theta);
        });
        auto const weightSum = buildCDF2D(extent, envMapCDF_, envMapRows_);
        luminanceIntegral = weightSum * 2.0F * std::numbers::pi_v<float> *
                            std::numbers::pi_v<float> / static_cast<float>(numPixels);
        if (weightSum > 0.0F) {
            distribution = {
                .condCDF = envMapCDF_.data(),
                .rowCDF = envMapRows_.data(),
                .extent = extent,
            };
        }
    }

    envMap_.emplace(envMap.getImpl(distribution));
    return envMap.estimatePower(sceneRadius, luminanceIntegral);
}

void EmbreeLightSampler::clear() noexcept {
    tableData_.clear();
    powerCDF_.clear();
    envMapCDF_.clear();
    envMapRows_.clear();
    envMap_.reset();
}

EmbreeLightSampler::Impl EmbreeLightSampler::getImpl() const noexcept {
    return {
        .table =
            {
                .pointLights = tableData_.pointLights.data(),
                .primIndices = tableData_.primIndices.data(),
                .primAreaScales = tableData_.primAreaScales.data(),
                .envMap = envMap_ ? &*envMap_ : nullptr,
            },
        .lights = tableData_.handles.data(),
        .pointSlots = tableData_.pointSlots.data(),
        .primSlots = tableData_.primSlots.data(),
        .envMapSlot = tableData_.envMapSlot,
        .power = {
            .cdf = powerCDF_.data(),
            .sum = powerCDF_.empty() ? 0.0F : powerCDF_.back(),
            .numLights = static_cast<std::uint32_t>(tableData_.handles.size()),
        },
    };
}
} // namespace flux
