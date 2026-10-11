#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "flux/Optix/OptixAccumulationBuffer.h"
#include "flux/Optix/OptixAccumulationBufferPImpl.h"
#include "flux/Optix/OptixHandler.h"

namespace flux {
OptixAccumulationBuffer::pImpl::pImpl(
    OptixDeviceSession const &deviceSession, Vec2u resolution, FilmChannels channels
)
    : session(&deviceSession), cudaContextGuard(std::in_place, deviceSession.getPrimaryContext()),
      storage(std::in_place, deviceSession.getStream()) {
    detail::checkFilmShape(resolution, channels);

    film.width = resolution.x();
    film.height = resolution.y();
    auto const pixelCount =
        static_cast<std::size_t>(film.width) * static_cast<std::size_t>(film.height);
    storage.forEach([&](auto &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        if (!any(channels & Channel::flag))
            return;
        channel.buffer.resize(pixelCount);
        film.channels.template get<Channel>().data = channel.buffer.data();
    });

    // The allocations are stream-ordered, so wait for them before the buffer is used.
    cudaCheck(cudaStreamSynchronize(deviceSession.getStream()));
    cudaContextGuard.reset();
}

OptixAccumulationBuffer::pImpl::~pImpl() {
    // Member destruction enqueues stream releases that need the session's context current. C++
    // destroys the members declared after cudaContextGuard first, then cudaContextGuard, which
    // pops.
    cudaContextGuard.emplace(session->getPrimaryContext(), std::nothrow);
}

FilmChannels OptixAccumulationBuffer::pImpl::getChannels() const noexcept {
    auto held = FilmChannels::None;
    film.channels.forEach([&](auto const &view) {
        using Channel = typename std::remove_reference_t<decltype(view)>::ChannelType;
        if (view.data)
            held = held | Channel::flag;
    });
    return held;
}

OptixAccumulationBuffer::OptixAccumulationBuffer(
    OptixHandler &handler, Vec2u resolution, FilmChannels channels
)
    : pImpl_(std::make_unique<pImpl>(handler.getDeviceSession(), resolution, channels)) {}

OptixAccumulationBuffer::~OptixAccumulationBuffer() = default;

void OptixAccumulationBuffer::clear() noexcept { pImpl_->samples = 0; }

std::uint64_t OptixAccumulationBuffer::getSampleCount() const noexcept { return pImpl_->samples; }

Vec2u OptixAccumulationBuffer::getResolution() const noexcept {
    return {pImpl_->film.width, pImpl_->film.height};
}

FilmChannels OptixAccumulationBuffer::getChannels() const noexcept { return pImpl_->getChannels(); }

void OptixAccumulationBuffer::download(Film &film) {
    auto &impl = *pImpl_;
    if (film.getWidth() != impl.film.width || film.getHeight() != impl.film.height ||
        film.getChannels() != impl.getChannels())
        throw std::invalid_argument(
            "OptixAccumulationBuffer: film differs in resolution or channels from the buffer"
        );
    if (impl.samples == 0)
        return;

    CudaContextGuard const guard(impl.session->getPrimaryContext());
    auto const destination = film.getImpl();
    impl.storage.forEach([&](auto &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        auto *output = destination.channels.template get<Channel>().data;
        channel.buffer.copyToHost({output, channel.buffer.size()}, impl.session->getStream());
    });
}
} // namespace flux
