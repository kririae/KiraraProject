#pragma once

#include <cstdint>
#include <optional>

/// \brief Decides how the samples of a render reach its target, and which launches end in a
/// denoise.
class HdFLuxSampleSchedule {
public:
    /// \brief How the samples of a render are split into launches.
    enum class Mode {
        /// A launch adds at most the samples per launch of the schedule.
        Interactive,
        /// A launch adds the remaining samples, within the path limit.
        Batch,
    };

    /// \brief When a render denoises its color.
    struct DenoiseCadence {
        /// The samples per pixel from which the color output shows the denoised color.
        int startSample;
        /// How many launches pass between two denoises.
        int launchesPerDenoise;

        bool operator==(DenoiseCadence const &) const = default;
    };

    /// \brief What the outputs receive.
    enum class Publication {
        /// No output changes.
        None,
        /// Every output receives the accumulation.
        Noisy,
        /// The color output receives the denoised color of the accumulation, and every other
        /// output the accumulation.
        Denoised,
    };

    /// \brief Creates a schedule that completes at \p target samples per pixel.
    ///
    /// An interactive launch adds at most \p interactivePerLaunch samples per pixel. Without a
    /// cadence, the schedule never denoises.
    HdFLuxSampleSchedule(
        int target, int interactivePerLaunch, Mode mode = Mode::Interactive,
        std::optional<DenoiseCadence> denoiseCadence = std::nullopt
    );

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

    [[nodiscard]] bool Denoises() const noexcept { return denoiseCadence_.has_value(); }

    /// \brief Returns what the outputs receive.
    ///
    /// A render denoises at the first launch that reaches the start sample, then once every
    /// launches per denoise launches, and always at the target.
    ///
    /// \param samples The sample count of the accumulation.
    /// \param launchesSinceDenoise The launches since the last denoise of the accumulation, or
    /// nothing before its first denoise.
    [[nodiscard]] Publication GetPublication(
        std::uint64_t samples, std::optional<std::uint32_t> launchesSinceDenoise
    ) const noexcept;

    bool operator==(HdFLuxSampleSchedule const &) const = default;

private:
    std::uint64_t target_;
    std::uint32_t interactivePerLaunch_;
    Mode mode_;

    /// The start sample is at least 0, and the launches per denoise at least 1.
    std::optional<DenoiseCadence> denoiseCadence_;
};
