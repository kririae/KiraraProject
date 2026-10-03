#include "flux/Optix/OptixImagePool.h"

#include "flux/Scene/ImageAssetPImpl.h"

namespace flux {
void OptixImagePool::add(std::span<Ref<ImageAsset const> const> assets) {
    if (assets.empty())
        return;

    // Upload each mip chain into a new entry. The host pixels must outlive their copies.
    std::vector<Entry> entries;
    std::vector<std::vector<ImageAsset::ImageBuffer>> pixels;
    entries.reserve(assets.size());
    pixels.reserve(assets.size());
    try {
        for (auto const &asset : assets) {
            auto const &levels = pixels.emplace_back(asset->readMipChain());
            auto const &image = levels.front();

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

            auto array = cudaMipmappedArray_t{};
            cudaCheck(cudaMallocMipmappedArray(
                &array, &channelDesc, cudaExtent{image.extent.x(), image.extent.y(), 0},
                static_cast<unsigned int>(levels.size())
            ));
            auto &entry = entries.emplace_back(
                Entry{
                    .owner = Array(array),
                    .image = {
                        .array = array,
                        .componentType = image.componentType,
                        .colorSpace = image.colorSpace,
                        .numLevels = static_cast<std::uint32_t>(levels.size()),
                        .componentCount = asset->getComponentCount(),
                    },
                }
            );

            for (auto level = std::size_t{}; level < levels.size(); ++level) {
                auto levelArray = cudaArray_t{};
                cudaCheck(cudaGetMipmappedArrayLevel(
                    &levelArray, entry.image.array, static_cast<unsigned int>(level)
                ));
                auto const &levelImage = levels[level];
                auto const levelPixels = levelImage.getPixels();
                auto const rowBytes = levelPixels.size_bytes() / levelImage.extent.y();
                // clang-format off
                cudaCheck(cudaMemcpy2DToArrayAsync(
                    /* dst =     */ levelArray,
                    /* wOffset = */ 0,
                    /* hOffset = */ 0,
                    /* src =     */ levelPixels.data(),
                    /* spitch =  */ rowBytes,
                    /* width =   */ rowBytes,
                    /* height =  */ levelImage.extent.y(),
                    /* kind =    */ cudaMemcpyHostToDevice,
                    /* stream =  */ getStream()
                ));
                // clang-format on
            }
        }
        cudaCheck(cudaStreamSynchronize(getStream()));
    } catch (...) {
        // Complete the queued copies before their host pixels are destroyed.
        cudaCheck<false>(cudaStreamSynchronize(getStream()));
        throw;
    }

    entries_.reserve(entries_.size() + entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
        entries_.try_emplace(assets[i]->getId(), std::move(entries[i]));
}

void OptixImagePool::remove(std::span<std::size_t const> ids) {
    for (auto const id : ids)
        entries_.erase(id);
}

std::vector<std::size_t> OptixImagePool::getIds() const {
    std::vector<std::size_t> ids;
    ids.reserve(entries_.size());
    for (auto const &[id, entry] : entries_)
        ids.push_back(id);
    return ids;
}

OptixImage OptixImagePool::getImpl(std::size_t id) const { return entries_.at(id).image; }
} // namespace flux
