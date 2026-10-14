#include "flux/Embree/EmbreeAccumulationBuffer.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

#include "flux/Embree/EmbreeAccumulationBufferPImpl.h"

namespace flux {
EmbreeAccumulationBuffer::pImpl::pImpl(FilmSpec const &spec) {
    detail::checkFilmSpec(spec);

    film.width = spec.resolution.x();
    film.height = spec.resolution.y();
    auto const pixelCount =
        static_cast<std::size_t>(film.width) * static_cast<std::size_t>(film.height);
    storage.forEach([&](auto &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        if (!any(spec.channels & Channel::flag))
            return;
        channel.values.assign(pixelCount, typename Channel::Value{});
        film.channels.template get<Channel>().data = channel.values.data();
    });
}

FilmSpec EmbreeAccumulationBuffer::pImpl::getSpec() const noexcept {
    auto held = FilmChannels::None;
    film.channels.forEach([&](auto const &view) {
        using Channel = typename std::remove_reference_t<decltype(view)>::ChannelType;
        if (view.data)
            held = held | Channel::flag;
    });
    return {.resolution = {film.width, film.height}, .channels = held};
}

EmbreeAccumulationBuffer::EmbreeAccumulationBuffer(FilmSpec const &spec)
    : pImpl_(std::make_unique<pImpl>(spec)) {}

EmbreeAccumulationBuffer::~EmbreeAccumulationBuffer() = default;

void EmbreeAccumulationBuffer::clear() noexcept { pImpl_->samples = 0; }

std::uint64_t EmbreeAccumulationBuffer::getSampleCount() const noexcept { return pImpl_->samples; }

FilmSpec EmbreeAccumulationBuffer::getSpec() const noexcept { return pImpl_->getSpec(); }

void EmbreeAccumulationBuffer::download(Film &film) {
    auto &impl = *pImpl_;
    if (film.getSpec() != impl.getSpec())
        throw std::invalid_argument(
            "EmbreeAccumulationBuffer: film differs in resolution or channels from the buffer"
        );
    if (impl.samples == 0)
        return;

    auto const destination = film.getImpl();
    impl.storage.forEach([&](auto const &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        std::ranges::copy(channel.values, destination.channels.template get<Channel>().data);
    });
}
} // namespace flux
