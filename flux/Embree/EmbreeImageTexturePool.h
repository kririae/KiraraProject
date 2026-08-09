#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "flux/Shading/Texture.h"

namespace flux {
/// \brief Provides ImageTexture lookups for Embree.
class EmbreeImageTexturePool final : private Noncopyable {
private:
    struct Entry;

public:
    struct Impl {
        Entry const *textures{};

        /// \brief Samples image texture \p index at normalized UV coordinates.
        /// \pre \p index refers to an entry built by \c EmbreeImageTexturePool.
        [[nodiscard]] Vec4f eval4f(std::uint32_t index, Vec2f uv) const noexcept;
    };

    EmbreeImageTexturePool();
    ~EmbreeImageTexturePool();

    /// \brief Rebuilds image textures for the next render.
    ///
    /// The input order defines ImageTexture::Impl::imageTextureIndex.
    /// After this function throws, call \c build again before rendering.
    void build(std::span<Ref<ImageTexture const> const> textures);
    void clear() noexcept;

    /// \brief Returns a view valid until the next \c build or \c clear.
    [[nodiscard]] Impl getImpl() const noexcept {
        return {
            .textures = textures_.data(),
        };
    }

private:
    std::vector<Entry> textures_;
};

static_assert(std::is_standard_layout_v<EmbreeImageTexturePool::Impl>);
static_assert(std::is_trivially_copyable_v<EmbreeImageTexturePool::Impl>);
} // namespace flux
