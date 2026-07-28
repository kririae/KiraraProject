#include "flux/Optix/OptixRenderProductPool.h"

#include <cstddef>

#include "flux/Scene/RenderProduct.h"

namespace flux {
OptixRenderProductPool::Entry &OptixRenderProductPool::getOrCreate(RenderProduct const &product) {
    auto &entry = entries_.try_emplace(&product, product, getStream()).first->second;
    auto const &film = product.getFilm();
    if (entry.film.width == film.getWidth() && entry.film.height == film.getHeight() &&
        entry.enabledChannels == film.getChannels())
        return entry;

    auto const pixelCount =
        static_cast<std::size_t>(film.getWidth()) * static_cast<std::size_t>(film.getHeight());
    entry.storage.forEach([&](auto &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        channel.buffer.resize(film.hasChannel(Channel::flag) ? pixelCount : 0);
    });
    entry.film.width = film.getWidth();
    entry.film.height = film.getHeight();
    entry.film.channels.forEach([&](auto &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        channel.data = entry.storage.template get<Channel>().buffer.data();
    });
    entry.enabledChannels = film.getChannels();
    entry.accumulation.reset();
    return entry;
}

OptixRenderProductPool::Entry const *
OptixRenderProductPool::find(RenderProduct const &product) const noexcept {
    auto const iterator = entries_.find(&product);
    return iterator == entries_.end() ? nullptr : &iterator->second;
}

void OptixRenderProductPool::erase(RenderProduct const &product) noexcept {
    entries_.erase(&product);
}

void OptixRenderProductPool::resetAccumulation() noexcept {
    for (auto &item : entries_)
        item.second.accumulation.reset();
}
} // namespace flux
