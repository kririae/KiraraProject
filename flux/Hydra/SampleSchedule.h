#pragma once

#include <cstdint>

/// \brief Splits a render of a target number of samples per pixel into launches.
class HdFluxSampleSchedule {
public:
    /// \brief How the samples of a render are split into launches.
    enum class Mode {
        /// A launch adds at most the samples per launch of the schedule.
        Interactive,
        /// A launch adds the remaining samples, within the path limit.
        Batch,
    };

    /// \brief Creates a schedule that completes at \p target samples per pixel.
    ///
    /// An interactive launch adds at most \p interactivePerLaunch samples per pixel.
    HdFluxSampleSchedule(int target, int interactivePerLaunch, Mode mode = Mode::Interactive);

    /// \brief Returns the samples per pixel at which the render is complete.
    [[nodiscard]] std::uint64_t GetTarget() const noexcept { return target_; }

    /// \brief Returns the samples per pixel that the next launch adds.
    ///
    /// \p accumulated is the samples per pixel so far. \p pixelCount is the pixels of the film.
    /// The result is 0 once \p accumulated reaches the target.
    /// It is at least 1 before that. The paths of the launch, which are \p pixelCount times its
    /// samples, number at most 2^32 - 1, unless one sample per pixel already exceeds that.
    ///
    /// \pre \p pixelCount is nonzero.
    [[nodiscard]] std::uint32_t
    GetNextSampleCount(std::uint64_t accumulated, std::uint64_t pixelCount) const;

private:
    std::uint64_t target_;
    std::uint32_t interactivePerLaunch_;
    Mode mode_;
};
