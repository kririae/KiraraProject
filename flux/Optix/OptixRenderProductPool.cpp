#include "flux/Optix/OptixRenderProductPool.h"

#include "flux/Scene/RenderProduct.h"

namespace flux {
Film::DeviceImpl OptixRenderProductPool::acquire(RenderProduct const &product) {
    auto &entry = entries_.try_emplace(product.getContextId(), getStream()).first->second;
    auto const &film = product.getFilm();
    entry.normal.resize(static_cast<std::size_t>(film.getWidth()) * film.getHeight());
    return {
        .width = film.getWidth(),
        .height = film.getHeight(),
        .normal = entry.normal.data(),
    };
}
} // namespace flux
