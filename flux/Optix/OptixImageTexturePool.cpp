#include "flux/Optix/OptixImageTexturePool.h"

#include <unordered_map>

#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/ImageAssetPImpl.h"

namespace flux {
OptixImageTexturePool::~OptixImageTexturePool() noexcept { clear(); }

void OptixImageTexturePool::build(std::span<Ref<ImageTexture const> const> textures) {
    auto const toCudaAddressMode = [](ImageTextureAddressMode mode) {
        switch (mode) {
        case ImageTextureAddressMode::Wrap: return cudaAddressModeWrap;
        case ImageTextureAddressMode::Clamp: return cudaAddressModeClamp;
        case ImageTextureAddressMode::Mirror: return cudaAddressModeMirror;
        case ImageTextureAddressMode::Border: return cudaAddressModeBorder;
        }
        KIRA_UNREACHABLE();
    };

    struct ArrayEntry {
        cudaArray_t array{};
        ImageComponentType componentType{};
        ImageTransform pendingTransform{};
    };

    auto const createTextureObject = [](ArrayEntry const &image, cudaTextureAddressMode addressMode,
                                        cudaTextureFilterMode filterMode) {
        auto resource = cudaResourceDesc{};
        resource.resType = cudaResourceTypeArray;
        resource.res.array.array = image.array;

        auto texture = cudaTextureDesc{};
        texture.addressMode[0] = addressMode;
        texture.addressMode[1] = addressMode;
        texture.filterMode = filterMode;
        texture.readMode = image.componentType == ImageComponentType::UNorm8
                               ? cudaReadModeNormalizedFloat
                               : cudaReadModeElementType;
        texture.sRGB = image.pendingTransform == ImageTransform::SRGB;
        texture.normalizedCoords = 1;

        auto result = cudaTextureObject_t{};
        cudaCheck(cudaCreateTextureObject(&result, &resource, &texture, nullptr));
        return result;
    };

    clear();
    arrays_.reserve(textures.size());
    textureObjects_.reserve(textures.size());
    staging_.reserve(textures.size());
    // Image textures share pixels but keep their own address and filter modes.
    std::unordered_map<ImageAsset const *, ArrayEntry> arraysByAsset;
    arraysByAsset.reserve(textures.size());

    for (auto const &texture : textures) {
        auto const &asset = texture->getImageAsset();
        auto [iterator, inserted] = arraysByAsset.try_emplace(asset.get());
        if (inserted) {
            // Keep one- and two-component arrays compact.
            auto const image = asset->read();
            auto bits = 0;
            auto kind = cudaChannelFormatKindUnsigned;
            switch (image.componentType) {
            case ImageComponentType::UNorm8:
                bits = 8;
                kind = cudaChannelFormatKindUnsigned;
                break;
            case ImageComponentType::Float16:
                bits = 16;
                kind = cudaChannelFormatKindFloat;
                break;
            case ImageComponentType::Float32:
                bits = 32;
                kind = cudaChannelFormatKindFloat;
                break;
            }

            auto const channelDesc = cudaCreateChannelDesc(
                bits, image.componentCount >= 2 ? bits : 0, image.componentCount == 4 ? bits : 0,
                image.componentCount == 4 ? bits : 0, kind
            );
            cudaCheck(cudaMallocArray(
                &iterator->second.array, &channelDesc, image.extent.x(), image.extent.y()
            ));
            arrays_.push_back(iterator->second.array);
            iterator->second.componentType = image.componentType;
            iterator->second.pendingTransform = image.pendingTransform;

            auto const pixels = image.getPixels();
            auto const rowBytes = pixels.size_bytes() / image.extent.y();
            // TODO(krr): Keep image buffers until the final stream sync and use
            // cudaMemcpy2DToArrayAsync. This blocking copy keeps host memory bounded to one
            // image, but image reads and uploads cannot overlap.
            cudaCheck(cudaMemcpy2DToArray(
                iterator->second.array, 0, 0, pixels.data(), rowBytes, rowBytes, image.extent.y(),
                cudaMemcpyHostToDevice
            ));
        }

        auto const addressMode = toCudaAddressMode(texture->getAddressMode());
        auto const filterMode = texture->getFilterMode() == ImageTextureFilterMode::Linear
                                    ? cudaFilterModeLinear
                                    : cudaFilterModePoint;
        auto const textureObject = createTextureObject(iterator->second, addressMode, filterMode);
        textureObjects_.push_back(textureObject);
        staging_.push_back({
            .texture = textureObject,
            .componentMapping = texture->getComponentMapping(),
            .componentCount = asset->getComponentCount(),
        });
    }

    deviceTextures_.copyFromHost(staging_);
}

void OptixImageTexturePool::clear() noexcept {
    // Texture objects borrow arrays, so destroy all views before storage.
    deviceTextures_.clear();
    for (auto const texture : textureObjects_)
        cudaCheck<false>(cudaDestroyTextureObject(texture));
    for (auto *const array : arrays_)
        cudaCheck<false>(cudaFreeArray(array));
    textureObjects_.clear();
    arrays_.clear();
    staging_.clear();
}
} // namespace flux
