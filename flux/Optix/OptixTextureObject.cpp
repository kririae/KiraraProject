#include "flux/Optix/OptixTextureObject.h"

#include "flux/Core/RayFootprint.h"

namespace flux {
CudaTextureObject createTextureObject(
    OptixImage const &image, ImageTextureAddressMode addressMode, ImageTextureFilterMode filterMode
) {
    auto const cudaAddressMode = [&] {
        switch (addressMode) {
        case ImageTextureAddressMode::Wrap: return cudaAddressModeWrap;
        case ImageTextureAddressMode::Clamp: return cudaAddressModeClamp;
        case ImageTextureAddressMode::Mirror: return cudaAddressModeMirror;
        case ImageTextureAddressMode::Border: return cudaAddressModeBorder;
        }
        KIRA_UNREACHABLE();
    }();

    auto resource = cudaResourceDesc{};
    resource.resType = cudaResourceTypeMipmappedArray;
    resource.res.mipmap.mipmap = image.array;

    auto texture = cudaTextureDesc{};
    texture.addressMode[0] = cudaAddressMode;
    texture.addressMode[1] = cudaAddressMode;
    texture.filterMode =
        filterMode == ImageTextureFilterMode::Linear ? cudaFilterModeLinear : cudaFilterModePoint;
    texture.mipmapFilterMode = cudaFilterModeLinear;
    texture.maxAnisotropy = RayFootprint::maxAnisotropy;
    texture.maxMipmapLevelClamp = static_cast<float>(image.numLevels - 1);
    texture.readMode = image.componentType == ImageComponentType::UNorm8
                           ? cudaReadModeNormalizedFloat
                           : cudaReadModeElementType;
    // CUDA applies sRGB decoding before texture filtering.
    texture.sRGB = image.colorSpace == ImageColorSpace::SRGB;
    texture.normalizedCoords = 1;

    auto result = cudaTextureObject_t{};
    cudaCheck(cudaCreateTextureObject(&result, &resource, &texture, nullptr));
    return CudaTextureObject(result);
}
} // namespace flux
