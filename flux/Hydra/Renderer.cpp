#include "flux/Hydra/Renderer.h"

#include <pxr/base/work/loops.h>

#include <optional>
#include <span>
#include <vector>

#include "flux/Hydra/Camera.h"
#include "flux/Hydra/RenderBuffer.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/RenderSettings.h"
#include "flux/Scene/Context.h"

namespace {
/// \brief Maps the depth channel of a pixel to its clip depth.
///
/// The view-space z of a pixel is -x / y, and its clip depth is (P22 z + P32) / (P23 z + P33)
/// mapped from [-1, 1] to [0, 1]. Scaling both terms by y leaves one division. A pixel without a
/// hit has x = y = 0, and its miss flag added to both terms gives 0.5 / 1 + 0.5 = 1.
struct ClipDepthMap {
    float numeratorX;
    float numeratorY;
    float denominatorX;
    float denominatorY;

    void operator()(std::span<flux::Vec2f const> depths, float *__restrict clipDepths) const {
        for (std::size_t index = 0; index < depths.size(); ++index) {
            auto const &depth = depths[index];
            auto const missed = static_cast<float>(depth.y() == 0.0F);
            auto const numerator = numeratorX * depth.x() + numeratorY * depth.y() + 0.5F * missed;
            auto const denominator = denominatorX * depth.x() + denominatorY * depth.y() + missed;
            clipDepths[index] = numerator / denominator + 0.5F;
        }
    }
};

/// \brief Converts \p depths to the depths of Hydra's depth AOV, which \p projection maps to
/// [0, 1].
///
/// A pixel that no sample hit is at the far plane, 1.
[[nodiscard]] std::vector<float>
toClipDepths(std::span<flux::Vec2f const> depths, pxr::GfMatrix4d const &projection) {
    auto const map = ClipDepthMap{
        .numeratorX = static_cast<float>(-0.5 * projection[2][2]),
        .numeratorY = static_cast<float>(0.5 * projection[3][2]),
        .denominatorX = static_cast<float>(-projection[2][3]),
        .denominatorY = static_cast<float>(projection[3][3]),
    };

    std::vector<float> clipDepths(depths.size());
    pxr::WorkParallelForN(depths.size(), [&](std::size_t begin, std::size_t end) {
        map(depths.subspan(begin, end - begin), clipDepths.data() + begin);
    });
    return clipDepths;
}

void assignChannel(
    HdFluxRenderBuffer &buffer, flux::Film const &film, flux::FilmChannels channel,
    pxr::GfMatrix4d const &projection
) {
    switch (channel) {
    case flux::FilmChannels::Color: buffer.Assign(film.getChannel<flux::ColorChannel>()); return;
    case flux::FilmChannels::Normal: buffer.Assign(film.getChannel<flux::NormalChannel>()); return;
    case flux::FilmChannels::Albedo: buffer.Assign(film.getChannel<flux::AlbedoChannel>()); return;
    case flux::FilmChannels::Depth:
        buffer.Assign(toClipDepths(film.getChannel<flux::DepthChannel>(), projection));
        return;
    default: return;
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

    for (auto const &output : outputs) {
        auto &buffer = *output.buffer;

        if (hasSamples && output.channel != flux::FilmChannels::None) {
            if (launched || !buffer.IsConverged())
                assignChannel(buffer, image_->film, output.channel, spec_.view->projection);
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
