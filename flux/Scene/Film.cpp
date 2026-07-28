#include "flux/Scene/Film.h"

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
    width_ = width;
    height_ = height;
}

void Film::setChannels(FilmChannels channels) {
    if ((bits(channels) & ~bits(FilmChannels::All)) != 0)
        throw kira::Anyhow("Film: channel selection contains an unknown bit");
    channels_ = channels;
}

bool Film::hasChannel(FilmChannels channel) const noexcept {
    auto const requested = bits(channel);
    return requested != 0 && (bits(channels_) & requested) == requested;
}
} // namespace flux
