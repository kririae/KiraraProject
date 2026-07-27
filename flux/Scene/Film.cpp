#include "flux/Scene/Film.h"

#include "kira/Anyhow.h"

namespace flux {
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
} // namespace flux
