#include "flux/Embree/EmbreeRenderProductPool.h"

#include <cstddef>
#include <limits>
#include <type_traits>

#include "flux/Scene/RenderProduct.h"

namespace flux {
EmbreeRenderProductPool::Entry &EmbreeRenderProductPool::getOrCreate(RenderProduct const &product) {
    auto &entry = entries_.try_emplace(&product, product).first->second;
    auto const &film = product.getFilm();
    if (entry.film.width == film.getWidth() && entry.film.height == film.getHeight() &&
        entry.requestedChannels == film.getChannels())
        return entry;

    // Clear the Film view before resizing storage. Publish all channel pointers
    // together after every resize succeeds.
    entry.accumulation.reset();
    entry.film = {};
    entry.requestedChannels = FilmChannels::None;

    if (film.getHeight() > std::numeric_limits<std::size_t>::max() / film.getWidth())
        throw std::invalid_argument("EmbreeRenderProductPool: film dimensions are too large");
    auto const pixelCount =
        static_cast<std::size_t>(film.getWidth()) * static_cast<std::size_t>(film.getHeight());

    entry.storage.forEach([&](auto &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        if (film.hasChannel(Channel::flag))
            channel.values.assign(pixelCount, typename Channel::Value{});
        else
            channel.values = std::vector<typename Channel::Value>{};
    });

    entry.film.width = film.getWidth();
    entry.film.height = film.getHeight();
    entry.film.channels.forEach([&](auto &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        auto &values = entry.storage.template get<Channel>().values;
        channel.data = values.empty() ? nullptr : values.data();
    });
    entry.requestedChannels = film.getChannels();
    return entry;
}

EmbreeRenderProductPool::Entry const *
EmbreeRenderProductPool::find(RenderProduct const &product) const noexcept {
    auto const iterator = entries_.find(&product);
    return iterator == entries_.end() ? nullptr : &iterator->second;
}

EmbreeRenderProductPool::Entry *
EmbreeRenderProductPool::find(RenderProduct const &product) noexcept {
    auto const iterator = entries_.find(&product);
    return iterator == entries_.end() ? nullptr : &iterator->second;
}

void EmbreeRenderProductPool::erase(RenderProduct const &product) noexcept {
    entries_.erase(&product);
}

void EmbreeRenderProductPool::resetAccumulation() noexcept {
    for (auto &item : entries_)
        item.second.accumulation.reset();
}
} // namespace flux
