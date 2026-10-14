#include "flux/Hydra/SampleSchedule.h"

#include <pxr/base/tf/diagnostic.h>

#include <algorithm>
#include <limits>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
constexpr std::uint64_t maxPathCount{std::numeric_limits<std::uint32_t>::max()};
} // namespace

HdFluxSampleSchedule::HdFluxSampleSchedule(int target, int interactivePerLaunch, Mode mode)
    : target_{static_cast<std::uint64_t>(std::max(target, 0))},
      interactivePerLaunch_{static_cast<std::uint32_t>(std::max(interactivePerLaunch, 1))},
      mode_{mode} {
    if (target < 0)
        TF_WARN("convergedSamplesPerPixel is %d, using 0", target);

    if (interactivePerLaunch < 1)
        TF_WARN("flux:interactiveSamplesPerLaunch is %d, using 1", interactivePerLaunch);
}

std::uint32_t HdFluxSampleSchedule::GetNextSampleCount(
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
