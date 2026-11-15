#include "flux/Hydra/Renderer.h"

#include <pxr/base/work/loops.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "flux/Hydra/Camera.h"
#include "flux/Hydra/RenderBuffer.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/RenderSettings.h"
#include "flux/Scene/Context.h"

namespace {
/// \brief The terms of a projection that map the depth channel of a pixel to its clip depth.
struct ClipDepthTerms {
    float numeratorX;
    float numeratorY;
    float denominatorX;
    float denominatorY;
};

/// \brief Writes the clip depths of \p depths to \p clipDepths, with the terms of a projection.
///
/// The view-space z of a pixel is -x / y, and its clip depth is (P22 z + P32) / (P23 z + P33)
/// mapped from [-1, 1] to [0, 1]. Scaling both terms by y leaves one division. A pixel without a
/// hit has x = y = 0, and its miss flag added to both terms gives 0.5 / 1 + 0.5 = 1.
void toClipDepths(
    ClipDepthTerms terms, std::span<flux::Vec2f const> depths, float *__restrict clipDepths
) {
    for (std::size_t index = 0; index < depths.size(); ++index) {
        auto const &depth = depths[index];
        auto const missed = static_cast<float>(depth.y() == 0.0F);
        auto const numerator =
            terms.numeratorX * depth.x() + terms.numeratorY * depth.y() + 0.5F * missed;
        auto const denominator =
            terms.denominatorX * depth.x() + terms.denominatorY * depth.y() + missed;
        clipDepths[index] = numerator / denominator + 0.5F;
    }
}

/// \brief Converts \p depths to the depths of Hydra's depth AOV, which \p projection maps to
/// [0, 1].
///
/// A pixel that no sample hit is at the far plane, 1.
[[nodiscard]] std::vector<float>
toClipDepths(std::span<flux::Vec2f const> depths, pxr::GfMatrix4d const &projection) {
    auto const terms = ClipDepthTerms{
        .numeratorX = static_cast<float>(-0.5 * projection[2][2]),
        .numeratorY = static_cast<float>(0.5 * projection[3][2]),
        .denominatorX = static_cast<float>(-projection[2][3]),
        .denominatorY = static_cast<float>(projection[3][3]),
    };

    std::vector<float> clipDepths(depths.size());
    pxr::WorkParallelForN(depths.size(), [&](std::size_t begin, std::size_t end) {
        toClipDepths(terms, depths.subspan(begin, end - begin), clipDepths.data() + begin);
    });
    return clipDepths;
}

void writeChannel(
    HdFLuxRenderBuffer &buffer, flux::Film const &film, flux::FilmChannels channel,
    pxr::GfMatrix4d const &projection
) {
    switch (channel) {
    case flux::FilmChannels::Color: buffer.Write(film.getChannel<flux::ColorChannel>()); return;
    case flux::FilmChannels::Normal: buffer.Write(film.getChannel<flux::NormalChannel>()); return;
    case flux::FilmChannels::Albedo: buffer.Write(film.getChannel<flux::AlbedoChannel>()); return;
    case flux::FilmChannels::Depth:
        buffer.Write(toClipDepths(film.getChannel<flux::DepthChannel>(), projection));
        return;
    case flux::FilmChannels::ObjectId:
        buffer.Write(film.getChannel<flux::ObjectIdChannel>());
        return;
    case flux::FilmChannels::InstanceId:
        buffer.Write(film.getChannel<flux::InstanceIdChannel>());
        return;
    default: return;
    }
}

[[nodiscard]] std::optional<flux::FilmSpec> toFilmSpec(HdFLuxRenderSpec const &spec) {
    if (spec.channels == flux::FilmChannels::None || !spec.view)
        return std::nullopt;
    return flux::FilmSpec{.resolution = spec.resolution, .channels = spec.channels};
}
} // namespace

HdFLuxRenderer::Accumulation::Accumulation(
    flux::OptixHandler &handler, flux::FilmSpec const &spec, HdFLuxRenderSpec::View const &view
)
    : accumulationBuffer{handler, spec}, film{spec}, camera{flux::Camera::create()},
      start{std::chrono::steady_clock::now()} {
    HdFLuxSetCamera(
        *camera, view.worldToView, view.projection,
        static_cast<double>(spec.resolution.x()) / spec.resolution.y()
    );
}

HdFLuxRenderer::HdFLuxRenderer(HdFLuxRenderParam &param, pxr::HdRenderThread &renderThread)
    : param_{param}, renderThread_{renderThread},
      syncedVersion_{param.GetHandler().getSyncedVersion()},
      schedule_{HdFLuxDefaultConvergedSamplesPerPixel, HdFLuxDefaultInteractiveSamplesPerLaunch} {}

void HdFLuxRenderer::StartRender(
    HdFLuxRenderSpec const &spec, std::span<Output const> outputs,
    HdFLuxSampleSchedule const &schedule
) {
    if (param_.HasFailed())
        return;

    auto const syncedVersion = param_.GetHandler().getSyncedVersion();
    auto const imageChanged = syncedVersion != syncedVersion_ || spec != spec_;
    if (imageChanged || schedule != schedule_ || !std::ranges::equal(outputs, outputs_)) {
        renderThread_.StopRender();

        if (imageChanged) {
            syncedVersion_ = syncedVersion;
            spec_ = spec;
            auto const film = toFilmSpec(spec);
            accumulation_ =
                film ? std::make_unique<Accumulation>(param_.GetHandler(), *film, *spec.view)
                     : nullptr;
        }

        schedule_ = schedule;
        outputs_.assign(outputs.begin(), outputs.end());
        setConverged(false);
    }

    if (!IsConverged())
        renderThread_.StartRender();
}

bool HdFLuxRenderer::IsConverged() const {
    return param_.HasFailed() || std::ranges::all_of(outputs_, [](Output const &output) {
        return output.buffer->IsConverged();
    });
}

void HdFLuxRenderer::Render() noexcept try {
    while (!renderThread_.IsStopRequested() && !param_.HasFailed()) {
        if (!isAtTarget())
            launch();
        writeOutputs();
        publishProgress();

        if (isAtTarget()) {
            setConverged(true);
            return;
        }
    }
} catch (...) { param_.ReportFailure(); }

bool HdFLuxRenderer::isAtTarget() const {
    return !accumulation_ ||
           accumulation_->accumulationBuffer.getSampleCount() >= schedule_.GetTarget();
}

void HdFLuxRenderer::launch() {
    auto &accumulation = *accumulation_;
    auto const pixelCount = std::uint64_t{spec_.resolution.x()} * spec_.resolution.y();
    auto const count =
        schedule_.GetNextSampleCount(accumulation.accumulationBuffer.getSampleCount(), pixelCount);

    accumulation.stats +=
        param_.GetHandler().render(*accumulation.camera, accumulation.accumulationBuffer, count);
    accumulation.accumulationBuffer.download(accumulation.film);

    if (isAtTarget())
        accumulation.stats.log();
}

void HdFLuxRenderer::writeOutputs() const {
    auto const hasSamples = accumulation_ && accumulation_->accumulationBuffer.getSampleCount() > 0;

    for (auto const &output : outputs_)
        if (hasSamples && output.channel != flux::FilmChannels::None)
            writeChannel(
                *output.buffer, accumulation_->film, output.channel, spec_.view->projection
            );
        else if (!output.clearValue.IsEmpty())
            output.buffer->Clear(output.clearValue);
}

void HdFLuxRenderer::publishProgress() {
    if (!accumulation_) {
        percentDone_ = 0.0;
        totalClockTime_ = 0.0;
        return;
    }

    auto const target = schedule_.GetTarget();
    auto const samples = accumulation_->accumulationBuffer.getSampleCount();
    percentDone_ =
        target == 0 ? 100.0 : 100.0 * static_cast<double>(samples) / static_cast<double>(target);
    totalClockTime_ =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - accumulation_->start)
            .count();
}

void HdFLuxRenderer::setConverged(bool converged) const {
    for (auto const &output : outputs_)
        output.buffer->SetConverged(converged);
}
