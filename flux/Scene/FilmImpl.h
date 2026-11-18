#pragma once

#include <atomic>

#include "flux/Scene/Film.h"

namespace flux {
template <typename Channel>
    requires(Channel::accumulates)
KIRA_HOST_DEVICE inline void
Film::Impl::accumulate(Vec2u const &pixel, typename Channel::Value const &value) const noexcept {
    auto *data = channels.template get<Channel>().data;
    if (!data)
        return;

    auto &destination = data[static_cast<std::size_t>(pixel.y()) * width + pixel.x()];
    // The view and indexing are shared; only the atomic primitive depends on
    // the execution environment.
    for (std::size_t component = 0; component < Channel::Value::Size; ++component) {
#if defined(__CUDA_ARCH__)
        atomicAdd(&destination[component], value[component]);
#else
        std::atomic_ref<float>{destination[component]}.fetch_add(
            value[component], std::memory_order_relaxed
        );
#endif
    }
}

template <typename Channel>
    requires(!Channel::accumulates)
KIRA_HOST_DEVICE inline void
Film::Impl::set(Vec2u const &pixel, typename Channel::Value const &value) const noexcept {
    if (auto *data = channels.template get<Channel>().data)
        data[static_cast<std::size_t>(pixel.y()) * width + pixel.x()] = value;
}

namespace detail {
struct ClearFilmChannel {
    std::size_t index;

    template <typename Channel>
    KIRA_HOST_DEVICE void operator()(FilmChannelView<Channel> const &channel) const noexcept {
        if (channel.data)
            channel.data[index] = Channel::getClearValue();
    }
};

struct ScaleFilmChannel {
    std::size_t index;
    float factor;

    template <typename Channel>
    KIRA_HOST_DEVICE void operator()(FilmChannelView<Channel> const &channel) const noexcept {
        if constexpr (Channel::accumulates) {
            if (channel.data)
                channel.data[index] = channel.data[index] * factor;
        }
    }
};
} // namespace detail

KIRA_HOST_DEVICE inline void Film::Impl::clear(std::size_t index) const noexcept {
    channels.forEach(detail::ClearFilmChannel{.index = index});
}

KIRA_HOST_DEVICE inline void Film::Impl::scale(std::size_t index, float factor) const noexcept {
    channels.forEach(detail::ScaleFilmChannel{.index = index, .factor = factor});
}
} // namespace flux
