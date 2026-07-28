#pragma once

#include "flux/Scene/Film.h"

namespace flux {
template <typename Channel>
KIRA_DEVICE inline void Film::DeviceImpl::accumulate(
    Vec2u const &pixel, typename Channel::Value const &value
) const noexcept {
    auto *data = channels.template get<Channel>().data;
    if (!data)
        return;

    static_assert(std::is_same_v<typename Channel::Value, Vec3f>);
    auto &destination = data[static_cast<std::size_t>(pixel.y()) * width + pixel.x()];
    atomicAdd(&destination[0], value[0]);
    atomicAdd(&destination[1], value[1]);
    atomicAdd(&destination[2], value[2]);
}

namespace detail {
struct ScaleFilmChannel {
    std::size_t index;
    float factor;

    template <typename Channel>
    KIRA_HOST_DEVICE void operator()(FilmChannelView<Channel> const &channel) const noexcept {
        if (channel.data)
            channel.data[index] = channel.data[index] * factor;
    }
};
} // namespace detail

KIRA_DEVICE inline void Film::DeviceImpl::scale(std::size_t index, float factor) const noexcept {
    channels.forEach(detail::ScaleFilmChannel{.index = index, .factor = factor});
}
} // namespace flux
