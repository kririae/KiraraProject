#include "flux/Scene/Film.h"

#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace flux {
namespace {
[[nodiscard]] constexpr std::uint32_t bits(FilmChannels channels) noexcept {
    return static_cast<std::uint32_t>(channels);
}
} // namespace

void detail::checkFilmSpec(FilmSpec const &spec) {
    if (spec.resolution.x() == 0 || spec.resolution.y() == 0)
        throw std::invalid_argument("checkFilmSpec: resolution must be nonzero");
    if ((bits(spec.channels) & ~bits(FilmChannels::All)) != 0)
        throw std::invalid_argument("checkFilmSpec: channels contain an unknown bit");
}

Film::Film(FilmSpec const &spec) : width_(spec.resolution.x()), height_(spec.resolution.y()) {
    detail::checkFilmSpec(spec);

    auto const pixelCount = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    hostStorage_.forEach([&](auto &storage) {
        using Channel = typename std::remove_reference_t<decltype(storage)>::ChannelType;
        if ((bits(spec.channels) & bits(Channel::flag)) != 0)
            storage.values.assign(pixelCount, typename Channel::Value{});
    });
}

FilmChannels Film::getChannels() const noexcept {
    auto held = FilmChannels::None;
    hostStorage_.forEach([&](auto const &storage) {
        using Channel = typename std::remove_reference_t<decltype(storage)>::ChannelType;
        if (!storage.values.empty())
            held = held | Channel::flag;
    });
    return held;
}

bool Film::hasChannel(FilmChannels channel) const noexcept {
    auto const wanted = bits(channel);
    return wanted != 0 && (bits(getChannels()) & wanted) == wanted;
}

Film::Impl Film::getImpl() noexcept {
    auto result = Impl{
        .width = width_,
        .height = height_,
    };
    hostStorage_.forEach([&](auto &storage) {
        using Channel = typename std::remove_reference_t<decltype(storage)>::ChannelType;
        result.channels.template get<Channel>().data =
            storage.values.empty() ? nullptr : storage.values.data();
    });
    return result;
}
} // namespace flux
