#pragma once

#include <cstdint>
#include <optional>

#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixAccumulationBuffer.h"
#include "flux/Optix/OptixDeviceSession.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Film.h"

namespace flux {
template <typename Channel> struct AccumulatedChannel {
    using ChannelType = Channel;
    explicit AccumulatedChannel(cudaStream_t stream) noexcept : buffer(stream) {}

    DeviceBuffer<typename Channel::Value> buffer;
};

struct OptixAccumulationBuffer::pImpl final {
    pImpl(OptixDeviceSession const &session, FilmSpec const &spec);
    ~pImpl();

    /// \brief Returns the spec that \c film views.
    [[nodiscard]] FilmSpec getSpec() const noexcept;

    /// The session of the handler that built the buffer.
    /// It outlives the buffer.
    /// Its stream orders the storage.
    OptixDeviceSession const *session;

    /// Holds the primary context of \c session current while the members after it are built or
    /// destroyed. Their CUDA calls then use that context.
    ///
    /// It is empty unless a \c pImpl constructor or destructor is running.
    std::optional<CudaContextGuard> cudaContextGuard;

    /// One allocation for each channel that the buffer holds.
    /// The other channels have no allocation.
    FilmChannelListOf<AccumulatedChannel> storage;

    /// Device view of \c storage.
    /// A channel is held exactly when its pointer is not null.
    Film::Impl film;

    /// The number of samples per pixel.
    /// \c storage holds their mean.
    std::uint64_t samples{};
};
} // namespace flux
