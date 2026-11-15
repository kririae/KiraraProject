#include "flux/Hydra/SampleSchedule.h"

#include <pxr/base/tf/diagnostic.h>

#include <algorithm>
#include <limits>
#include <optional>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
constexpr std::uint64_t maxPathCount{std::numeric_limits<std::uint32_t>::max()};

[[nodiscard]] std::optional<HdFLuxSampleSchedule::DenoiseCadence>
boundDenoiseCadence(std::optional<HdFLuxSampleSchedule::DenoiseCadence> cadence) {
    if (!cadence)
        return std::nullopt;

    if (cadence->startSample < 0)
        TF_WARN("flux:denoiseStartSample is %d, using 0", cadence->startSample);
    if (cadence->launchesPerDenoise < 1)
        TF_WARN("flux:launchesPerDenoise is %d, using 1", cadence->launchesPerDenoise);

    return HdFLuxSampleSchedule::DenoiseCadence{
        .startSample = std::max(cadence->startSample, 0),
        .launchesPerDenoise = std::max(cadence->launchesPerDenoise, 1),
    };
}
} // namespace

HdFLuxSampleSchedule::HdFLuxSampleSchedule(
    int target, int interactivePerLaunch, Mode mode, std::optional<DenoiseCadence> denoiseCadence
)
    : target_{static_cast<std::uint64_t>(std::max(target, 0))},
      interactivePerLaunch_{static_cast<std::uint32_t>(std::max(interactivePerLaunch, 1))},
      mode_{mode}, denoiseCadence_{boundDenoiseCadence(denoiseCadence)} {
    if (target < 0)
        TF_WARN("convergedSamplesPerPixel is %d, using 0", target);

    if (interactivePerLaunch < 1)
        TF_WARN("flux:interactiveSamplesPerLaunch is %d, using 1", interactivePerLaunch);
}

std::uint32_t HdFLuxSampleSchedule::GetNextSampleCount(
    std::uint64_t accumulated, std::uint64_t pixelCount
) const {
    auto const remaining = target_ > accumulated ? target_ - accumulated : 0;

    // A film beyond the limit still gets one sample per pixel.
    auto const fitting = std::max<std::uint64_t>(1, maxPathCount / pixelCount);

    auto const wanted = mode_ == Mode::Batch
                            ? remaining
                            : std::min<std::uint64_t>(interactivePerLaunch_, remaining);

    return static_cast<std::uint32_t>(std::min(wanted, fitting));
}

HdFLuxSampleSchedule::Publication HdFLuxSampleSchedule::GetPublication(
    std::uint64_t samples, std::optional<std::uint32_t> launchesSinceDenoise
) const noexcept {
    if (!denoiseCadence_)
        return Publication::Noisy;
    if (samples >= target_)
        return Publication::Denoised;
    if (samples < static_cast<std::uint64_t>(denoiseCadence_->startSample))
        return Publication::Noisy;
    if (!launchesSinceDenoise ||
        *launchesSinceDenoise >= static_cast<std::uint32_t>(denoiseCadence_->launchesPerDenoise))
        return Publication::Denoised;
    return Publication::None;
}
