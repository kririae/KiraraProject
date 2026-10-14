#pragma once

#include <cstdint>

#include "flux/Embree/EmbreeAccumulationBuffer.h"
#include "flux/Scene/Film.h"
#include "kira/SmallVector.h"

namespace flux {
struct EmbreeAccumulationBuffer::pImpl final {
    explicit pImpl(FilmSpec const &spec);

    /// \brief Returns the spec that \c film views.
    [[nodiscard]] FilmSpec getSpec() const noexcept;

    template <typename Channel> struct ChannelStorage {
        using ChannelType = Channel;
        kira::SmallVector<typename Channel::Value, 0> values;
    };

    /// One allocation for each channel that the buffer holds.
    /// The other channels have no allocation.
    FilmChannelListOf<ChannelStorage> storage;

    /// Host view of \c storage.
    /// A channel is held exactly when its pointer is not null.
    Film::Impl film;

    /// The number of samples per pixel.
    /// \c storage holds their mean.
    std::uint64_t samples{};
};
} // namespace flux
