#include "flux/Scene/Film.h"

#include <limits>
#include <stdexcept>

#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] constexpr std::uint32_t bits(FilmChannels channels) noexcept {
    return static_cast<std::uint32_t>(channels);
}
} // namespace

Film::Film(std::uint32_t width, std::uint32_t height) : width_(width), height_(height) {
    if (width == 0 || height == 0)
        throw kira::Anyhow("Film: resolution must be nonzero");
}

void Film::setResolution(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0)
        throw kira::Anyhow("Film: resolution must be nonzero");
    if (width_ == width && height_ == height)
        return;
    width_ = width;
    height_ = height;
    invalidateDownload();
}

void Film::setChannels(FilmChannels channels) {
    if ((bits(channels) & ~bits(FilmChannels::All)) != 0)
        throw kira::Anyhow("Film: requested channels contain an unknown bit");
    if (channels_ == channels)
        return;
    channels_ = channels;
    invalidateDownload();
}

bool Film::hasChannel(FilmChannels channel) const noexcept {
    auto const requested = bits(channel);
    return requested != 0 && (bits(channels_) & requested) == requested;
}

Film::Impl Film::prepareDownload() {
    if (height_ > std::numeric_limits<std::size_t>::max() / width_)
        throw std::invalid_argument("Film: resolution exceeds host storage limits");
    auto const pixelCount = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);

    auto result = Impl{
        .width = width_,
        .height = height_,
    };
    hostStorage_.forEach([&](auto &storage) {
        using Channel = typename std::remove_reference_t<decltype(storage)>::ChannelType;
        auto const requested = hasChannel(Channel::flag);
        storage.values.resize_for_overwrite(requested ? pixelCount : 0);
        result.channels.template get<Channel>().data = requested ? storage.values.data() : nullptr;
    });
    return result;
}

void Film::invalidateDownload() noexcept {
    hostStorage_.forEach([](auto &storage) { storage.values.clear(); });
}
} // namespace flux
