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
/// Each render product has at most one entry. Camera or Film changes invalidate
/// the corresponding accumulation.
class EmbreeRenderProductPool final : private Noncopyable {
    friend class EmbreeHandler;

public:
    /// \brief Erases runtime storage for \p product.
    ///
    /// Does nothing if \p product has no entry.
    void erase(RenderProduct const &product) noexcept;

    /// \brief Clears every accumulation while keeping allocated Film storage.
    void resetAccumulation() noexcept;

private:
    template <typename Channel> struct FilmChannelStorage {
        using ChannelType = Channel;
        std::vector<typename Channel::Value> values;
    };

    /// Tracks the Camera snapshot and sample count of one accumulation.
    struct AccumulationState {
        /// Camera::Impl used to generate the samples.
        Camera::Impl camera;
        /// Number of samples accumulated per pixel.
        std::uint64_t samples;
    };

    struct Entry {
        explicit Entry(RenderProduct const &product) noexcept : product(&product) {}

        /// Keeps the map key alive for the entry's lifetime.
        Ref<RenderProduct const> product;
        FilmChannelListOf<FilmChannelStorage> storage;
        /// Non-owning view into \c storage.
        Film::Impl film;
        /// Requested channels used to build \c storage and \c film.
        FilmChannels requestedChannels{FilmChannels::None};
        /// Current accumulation. Empty after invalidation or failure.
        std::optional<AccumulationState> accumulation;
    };

    /// \brief Returns or creates runtime storage for \p product.
    ///
    /// The entry matches the current Film resolution and requested channels.
    /// Changing either resizes channel storage and clears accumulation.
    /// The reference remains valid until the entry is erased.
    [[nodiscard]] Entry &getOrCreate(RenderProduct const &product);

    [[nodiscard]] Entry *find(RenderProduct const &product) noexcept;
    [[nodiscard]] Entry const *find(RenderProduct const &product) const noexcept;

    std::unordered_map<RenderProduct const *, Entry> entries_;
};
} // namespace flux
