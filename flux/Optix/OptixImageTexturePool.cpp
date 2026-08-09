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
        ImageColorSpace colorSpace{};
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
        // CUDA applies sRGB decoding before texture filtering.
        texture.sRGB = image.colorSpace == ImageColorSpace::SRGB;
        texture.normalizedCoords = 1;

        auto result = cudaTextureObject_t{};
        cudaCheck(cudaCreateTextureObject(&result, &resource, &texture, nullptr));
        return result;
    };

    clear();
    arrays_.reserve(textures.size());
    textureObjects_.reserve(textures.size() * 2);
    staging_.reserve(textures.size());
    // Keep host pixels alive until the stream synchronization below.
    std::vector<ImageAsset::ImageBuffer> imageBuffers;
    imageBuffers.reserve(textures.size());

    // Bindings for the same ImageAsset share one CUDA array.
    std::unordered_map<ImageAsset const *, ArrayEntry> arraysByAsset;
    arraysByAsset.reserve(textures.size());

    try {
        // TODO(krr): Reuse CUDA arrays for unchanged ImageAssets.
        // ImageAsset::read uses the OIIO cache. Each build still creates a
        // complete host buffer and uploads every active image.
        auto const uploadImage = [&](ImageAsset const &asset) {
            auto &image = imageBuffers.emplace_back(asset.read());

            // Keep one- and two-component arrays compact.
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
            auto result = ArrayEntry{
                .componentType = image.componentType,
                .colorSpace = image.colorSpace,
            };
            cudaCheck(
                cudaMallocArray(&result.array, &channelDesc, image.extent.x(), image.extent.y())
            );
            arrays_.push_back(result.array);

            auto const pixels = image.getPixels();
            auto const rowBytes = pixels.size_bytes() / image.extent.y();
            // clang-format off
            cudaCheck(cudaMemcpy2DToArrayAsync(
                /* dst =     */ result.array,
                /* wOffset = */ 0,
                /* hOffset = */ 0,
                /* src =     */ pixels.data(),
                /* spitch =  */ rowBytes,
                /* width =   */ rowBytes,
                /* height =  */ image.extent.y(),
                /* kind =    */ cudaMemcpyHostToDevice,
                /* stream =  */ getStream()
            ));
            // clang-format on
            return result;
        };

        for (auto const &texture : textures) {
            auto const &asset = texture->getImageAsset();
            auto [iterator, inserted] = arraysByAsset.try_emplace(asset.get());
            if (inserted)
                iterator->second = uploadImage(*asset);

            auto const addressMode = toCudaAddressMode(texture->getAddressMode());
            auto const filterMode = texture->getFilterMode() == ImageTextureFilterMode::Linear
                                        ? cudaFilterModeLinear
                                        : cudaFilterModePoint;
            auto const textureObject =
                createTextureObject(iterator->second, addressMode, filterMode);
            textureObjects_.push_back(textureObject);

            auto pointTexture = textureObject;
            if (filterMode != cudaFilterModePoint) {
                pointTexture =
                    createTextureObject(iterator->second, addressMode, cudaFilterModePoint);
                textureObjects_.push_back(pointTexture);
            }

            staging_.push_back({
                .texture = textureObject,
                .pointTexture = pointTexture,
                .componentMapping = texture->getComponentMapping(),
                .componentCount = asset->getComponentCount(),
            });
        }

        deviceTextures_.copyFromHost(staging_);
        cudaCheck(cudaStreamSynchronize(getStream()));
    } catch (...) {
        // Complete queued copies before destroying their host buffers.
        cudaCheck<false>(cudaStreamSynchronize(getStream()));
        throw;
    }
}

void OptixImageTexturePool::clear() noexcept {
    // Destroy texture objects before their CUDA arrays.
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
