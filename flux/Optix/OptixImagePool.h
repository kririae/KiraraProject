#pragma once

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeletionQueue.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/ImageAsset.h"

namespace flux {
/// \brief One image in CUDA memory, and how to read it.
struct OptixImage {
    /// Mip chain of the image.
    cudaMipmappedArray_t array{};
    ImageComponentType componentType{};
    ImageColorSpace colorSpace{};
    std::uint32_t numLevels{};
    /// Number of components stored per texel: one, two, or four.
    std::uint8_t componentCount{};
};

/// \brief CUDA images of image assets, by ImageAsset ID.
///
/// The pool holds no ImageAsset, and knows no texture. Mutation is single-threaded, so callers
/// provide external synchronization.
class OptixImagePool final : private Noncopyable {
public:
    explicit OptixImagePool(DeletionQueue &deletionQueue) noexcept
        : deletionQueue_(deletionQueue) {}

    /// \brief Deletes all images once the stream finishes its current work.
    ~OptixImagePool();

    /// \brief Reads \p assets and uploads their mip chains.
    void add(std::span<Ref<ImageAsset const> const> assets);

    /// \brief Deletes images once the stream finishes its current work.
    void remove(std::span<std::size_t const> ids);

    /// \brief Returns whether the image \p id is present.
    [[nodiscard]] bool contains(std::size_t id) const noexcept { return entries_.contains(id); }

    /// \brief Returns the ID of every present image, in no particular order.
    [[nodiscard]] std::vector<std::size_t> getIds() const;

    /// \brief Returns the image \p id.
    /// \throw std::out_of_range If the image is absent.
    [[nodiscard]] OptixImage getImpl(std::size_t id) const;

private:
    /// Owns one CUDA mipmapped array.
    class Array final : private Noncopyable {
    public:
        explicit Array(cudaMipmappedArray_t handle) noexcept : handle_(handle) {}
        Array(Array &&other) noexcept { swap(*this, other); }
        Array &operator=(Array &&other) noexcept {
            Array replacement(std::move(other));
            swap(*this, replacement);
            return *this;
        }
        ~Array() {
            if (handle_)
                cudaCheck<false>(cudaFreeMipmappedArray(handle_));
        }

        [[nodiscard]] cudaMipmappedArray_t get() const noexcept { return handle_; }

        friend void swap(Array &lhs, Array &rhs) noexcept {
            using std::swap;
            swap(lhs.handle_, rhs.handle_);
        }

    private:
        cudaMipmappedArray_t handle_{};
    };

    /// Device storage of one image.
    struct Entry {
        /// Owns \c image.array.
        Array owner;
        OptixImage image;
    };

    /// \brief Copies the pixels of an image level into a CUDA array.
    /// \param destination The array. The level must fit in it.
    /// \param level The level. Kept alive until the copy finishes.
    /// \param deletionQueue The copy runs on its stream.
    /// \throw kira::Anyhow If CUDA fails.
    static void copyToArray(
        cudaArray_t destination, std::shared_ptr<ImageAsset::ImageBuffer const> level,
        DeletionQueue &deletionQueue
    );

    DeletionQueue &deletionQueue_;
    std::unordered_map<std::size_t, std::shared_ptr<Entry const>> entries_;
};
} // namespace flux
