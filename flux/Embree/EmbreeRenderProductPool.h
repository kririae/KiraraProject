#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Film.h"

namespace flux {
class EmbreeHandler;
class RenderProduct;

/// \brief Owns CPU runtime storage for render products.
///
/// Each render product has one entry. Camera and Film changes update that entry.
class EmbreeRenderProductPool final : private Noncopyable {
    friend class EmbreeHandler;

public:
    /// \brief Erases runtime storage for \p product.
    void erase(RenderProduct const &product) noexcept;

    /// \brief Clears accumulation and keeps Film storage.
    void resetAccumulation() noexcept;

private:
    /// \brief Storage for one typed Film channel.
    template <typename Channel> struct FilmChannelStorage {
        using ChannelType = Channel;
        std::vector<typename Channel::Value> values;
    };

    /// \brief Accumulation produced with one \c Camera::Impl.
    struct AccumulationState {
        /// Camera::Impl used for this accumulation.
        Camera::Impl camera;

        /// Samples in this accumulation.
        std::uint64_t samples;
    };

    /// \brief Runtime storage for one render product.
    struct Entry {
        explicit Entry(RenderProduct const &product) noexcept : product(&product) {}

        /// Keeps the product alive until \c erase.
        Ref<RenderProduct const> product;

        /// Host storage for channels requested by Film.
        FilmChannelListOf<FilmChannelStorage> storage;

        /// Film view of \c storage.
        Film::Impl film;

        /// Channels requested by Film and stored in this entry.
        FilmChannels requestedChannels{FilmChannels::None};

        /// Current accumulation. Empty after invalidation or failure.
        std::optional<AccumulationState> accumulation;
    };

    /// \brief Returns or creates the entry for \p product.
    [[nodiscard]] Entry &getOrCreate(RenderProduct const &product);

    /// \brief Returns the existing entry for \p product, if any.
    [[nodiscard]] Entry *find(RenderProduct const &product) noexcept;

    /// \brief Returns the existing entry for \p product, if any.
    [[nodiscard]] Entry const *find(RenderProduct const &product) const noexcept;

    std::unordered_map<RenderProduct const *, Entry> entries_;
};
} // namespace flux
