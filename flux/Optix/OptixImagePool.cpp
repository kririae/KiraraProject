#include "flux/Optix/OptixImagePool.h"

#include <cstddef>

#include "flux/Optix/DeviceBuffer.h"
#include "flux/Scene/ImageAssetPImpl.h"

namespace flux {
namespace {
/// \brief Copies rows of host bytes into a CUDA array.
/// \param destination The array. The rows must fit in it.
/// \param source The rows. Not used after the call returns.
/// \param rowBytes The size of one row in bytes. Positive, and divides the size of \p source.
/// \param deletionQueue The copy runs on its stream.
/// \throw kira::Anyhow If CUDA fails.
void copyToArray(
    cudaArray_t destination, std::span<std::byte const> source, std::size_t rowBytes,
    DeletionQueue &deletionQueue
) {
    auto shared = detail::copyToShared(source);

    // clang-format off
    auto const error = cudaMemcpy2DToArrayAsync(
        /* dst =     */ destination,
        /* wOffset = */ 0,
        /* hOffset = */ 0,
        /* src =     */ shared.get(),
        /* spitch =  */ rowBytes,
        /* width =   */ rowBytes,
        /* height =  */ source.size() / rowBytes,
        /* kind =    */ cudaMemcpyHostToDevice,
        /* stream =  */ deletionQueue.getStream()
    );
    // clang-format on

    deletionQueue.deleteLater(std::move(shared));
    cudaCheck(error);
}
} // namespace

OptixImagePool::~OptixImagePool() {
    // Uploads on the stream may still write the arrays.
    for (auto &[id, entry] : entries_)
        deletionQueue_.deleteLater(std::move(entry));
}

void OptixImagePool::add(std::span<Ref<ImageAsset const> const> assets) {
    if (assets.empty())
        return;

    std::vector<std::shared_ptr<Entry const>> entries;
    entries.reserve(assets.size());
    try {
        for (auto const &asset : assets) {
            auto const levels = asset->readMipChain();
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

            auto const &entry = *entries.emplace_back(
                std::make_shared<Entry const>(Entry{
                    .owner = Array(array),
                    .image = {
                        .array = array,
                        .componentType = image.componentType,
                        .colorSpace = image.colorSpace,
                        .numLevels = static_cast<std::uint32_t>(levels.size()),
                        .componentCount = asset->getComponentCount(),
                    },
                })
            );

            for (auto level = std::size_t{}; level < levels.size(); ++level) {
                auto levelArray = cudaArray_t{};
                cudaCheck(cudaGetMipmappedArrayLevel(
                    &levelArray, entry.image.array, static_cast<unsigned int>(level)
                ));

                auto const &levelImage = levels[level];
                auto const levelPixels = levelImage.getPixels();
                copyToArray(
                    levelArray, levelPixels, levelPixels.size_bytes() / levelImage.extent.y(),
                    deletionQueue_
                );
            }
        }

        entries_.reserve(entries_.size() + entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i)
            entries_.try_emplace(assets[i]->getId(), entries[i]);
    } catch (...) {
        // Uploads on the stream may still write the new arrays.
        for (auto const &asset : assets)
            entries_.erase(asset->getId());

        for (auto &entry : entries)
            deletionQueue_.deleteLater(std::move(entry));

        throw;
    }
}

void OptixImagePool::remove(std::span<std::size_t const> ids) {
    // Uploads on the stream may still write the arrays.
    for (auto const id : ids)
        if (auto node = entries_.extract(id))
            deletionQueue_.deleteLater(std::move(node.mapped()));
}

std::vector<std::size_t> OptixImagePool::getIds() const {
    std::vector<std::size_t> ids;
    ids.reserve(entries_.size());
    for (auto const &[id, entry] : entries_)
        ids.push_back(id);
    return ids;
}

OptixImage OptixImagePool::getImpl(std::size_t id) const { return entries_.at(id)->image; }
} // namespace flux
