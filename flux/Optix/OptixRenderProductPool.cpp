#include "flux/Optix/OptixRenderProductPool.h"

#include <cstddef>

#include "flux/Scene/RenderProduct.h"

namespace flux {
OptixRenderProductPool::Entry &OptixRenderProductPool::update(RenderProduct const &product) {
    auto &entry = entries_.try_emplace(&product, product, getStream()).first->second;
    auto const &film = product.getFilm();
    if (entry.film.width == film.getWidth() && entry.film.height == film.getHeight())
        return entry;

    entry.normal.resize(
        static_cast<std::size_t>(film.getWidth()) * static_cast<std::size_t>(film.getHeight())
    );
    entry.film = {
        .width = film.getWidth(),
        .height = film.getHeight(),
        .normal = entry.normal.data(),
    };
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
