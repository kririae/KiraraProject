#pragma once

#include <cstdint>
#include <memory>

#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "flux/Scene/Film.h"

namespace flux {
class EmbreeHandler;

/// \brief Holds the samples that an \c EmbreeHandler renders.
///
/// The samples live in host memory.
/// The resolution and the channels are fixed when the buffer is built.
///
/// For every pixel, each channel holds the mean of the samples in the buffer.
///
/// \remark A buffer is not thread-safe.
class EmbreeAccumulationBuffer final : private Noncopyable {
public:
    /// \brief Creates a buffer with \p resolution and \p channels.
    ///
    /// The buffer has no samples.
    ///
    /// \pre Both dimensions of \p resolution are nonzero.
    /// \pre \p channels has no unknown bit.
    EmbreeAccumulationBuffer(Vec2u resolution, FilmChannels channels);

    ~EmbreeAccumulationBuffer();

    /// \brief Removes all samples.
    ///
    /// The next render starts a new accumulation.
    void clear() noexcept;

    /// \brief Returns the number of samples per pixel in the buffer.
    [[nodiscard]] std::uint64_t getSampleCount() const noexcept;

    [[nodiscard]] Vec2u getResolution() const noexcept;

    /// \brief Returns the channels that the buffer holds.
    [[nodiscard]] FilmChannels getChannels() const noexcept;

    /// \brief Copies the mean of the samples into \p film.
    ///
    /// \pre \p film has the resolution and the channels of the buffer.
    void download(Film &film);

private:
    friend class EmbreeHandler;

    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux
