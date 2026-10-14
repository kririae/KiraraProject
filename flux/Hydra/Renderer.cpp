#include "flux/Hydra/Renderer.h"

#include <optional>
#include <span>

#include "flux/Hydra/Camera.h"
#include "flux/Hydra/RenderBuffer.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/RenderSettings.h"
#include "flux/Scene/Context.h"

namespace {
[[nodiscard]] std::span<flux::Vec3f const>
pixelsOf(flux::Film const &film, flux::FilmChannels channel) {
    switch (channel) {
    case flux::FilmChannels::Color: return film.getChannel<flux::ColorChannel>();
    case flux::FilmChannels::Normal: return film.getChannel<flux::NormalChannel>();
    case flux::FilmChannels::Albedo: return film.getChannel<flux::AlbedoChannel>();
    default: return {};
    }
}

[[nodiscard]] std::optional<flux::FilmSpec> getFilmSpec(HdFluxRenderSpec const &spec) {
    if (spec.channels == flux::FilmChannels::None || !spec.view)
        return std::nullopt;
    return flux::FilmSpec{.resolution = spec.resolution, .channels = spec.channels};
}
} // namespace

HdFluxRenderer::Image::Image(flux::OptixHandler &handler, flux::FilmSpec const &spec)
    : film{spec}, buffer{handler, spec} {}

HdFluxRenderer::HdFluxRenderer(HdFluxRenderParam &param)
    : param_{param},
      schedule_{HdFluxDefaultConvergedSamplesPerPixel, HdFluxDefaultInteractiveSamplesPerLaunch},
      contextVersion_{param.GetContext()->getVersion()}, camera_{flux::Camera::create()} {}

bool HdFluxRenderer::IsConverged() const {
    return param_.HasFailed() || !image_ ||
           image_->buffer.getSampleCount() >= schedule_.GetTarget();
}

void HdFluxRenderer::Render(HdFluxRenderSpec const &spec, std::span<Output const> outputs) {
    // The buffers of a bound AOV are polled until they converge, so a failure of the param ends
    // them.
    if (param_.HasFailed()) {
        setConverged(outputs);
        return;
    }

    bool restart{false};

    if (spec != spec_) {
        spec_ = spec;
        restart = true;

        auto const film = getFilmSpec(spec);
        if (!film)
            image_.reset();
        else if (!image_ || image_->buffer.getSpec() != *film)
            image_ = std::make_unique<Image>(param_.GetHandler(), *film);

        if (image_)
            HdFluxSetCamera(
                *camera_, spec.view->worldToView, spec.view->projection,
                static_cast<double>(spec.resolution.x()) / spec.resolution.y()
            );
    }

    if (auto const version = param_.GetContext()->getVersion(); version != contextVersion_) {
        contextVersion_ = version;
        restart = true;
    }

    if (restart && image_)
        image_->buffer.clear();

    bool launched{false};

    if (image_) {
        auto &buffer = image_->buffer;
        auto const pixelCount = std::uint64_t{spec.resolution.x()} * spec.resolution.y();

        if (auto const count = schedule_.GetNextSampleCount(buffer.getSampleCount(), pixelCount);
            count != 0) {
            param_.GetHandler().render(*camera_, buffer, count);
            buffer.download(image_->film);
            launched = true;
        }
    }

    auto const hasSamples = image_ && image_->buffer.getSampleCount() > 0;

    // A buffer that is not converged may not hold the image.
    for (auto const &output : outputs) {
        auto &buffer = *output.buffer;

        if (hasSamples && output.channel != flux::FilmChannels::None) {
            if (launched || !buffer.IsConverged())
                buffer.Assign(pixelsOf(image_->film, output.channel));
        } else if (!output.clearValue.IsEmpty()) {
            buffer.Clear(output.clearValue);
        }
    }

    setConverged(outputs);
}

void HdFluxRenderer::setConverged(std::span<Output const> outputs) const {
    auto const converged = IsConverged();
    for (auto const &output : outputs)
        output.buffer->SetConverged(converged);
}
