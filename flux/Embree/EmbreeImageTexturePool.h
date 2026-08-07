#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "flux/Shading/Texture.h"

namespace flux {
/// \brief Stores image textures used by Embree.
class EmbreeImageTexturePool final : private Noncopyable {
private:
    struct Image;
    struct Entry;

public:
    struct Impl {
        Image const *images{};
        Entry const *textures{};

        /// \brief Samples image texture \p index at normalized UV coordinates.
        /// \pre \p index refers to an entry built by \c EmbreeImageTexturePool.
        [[nodiscard]] Vec4f eval4f(std::uint32_t index, Vec2f uv) const noexcept;
    };

    EmbreeImageTexturePool();
    ~EmbreeImageTexturePool();

    /// \brief Rebuilds the image textures used by the next render.
    ///
    /// The input order defines ImageTexture::Impl::imageTextureIndex.
    /// A failure leaves the pool valid for another build, but not for rendering.
    void build(std::span<Ref<ImageTexture const> const> textures);
    void clear() noexcept;

    /// \brief Returns a view valid until the next \c build or \c clear.
    [[nodiscard]] Impl getImpl() const noexcept {
        return {
            .images = images_.data(),
            .textures = textures_.data(),
        };
    }

private:
    std::vector<Image> images_;
    std::vector<Entry> textures_;
};

static_assert(std::is_standard_layout_v<EmbreeImageTexturePool::Impl>);
static_assert(std::is_trivially_copyable_v<EmbreeImageTexturePool::Impl>);
} // namespace flux
