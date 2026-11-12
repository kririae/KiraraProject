#pragma once

#include <cstdint>
#include <memory>

#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "flux/Scene/Film.h"

namespace flux {
class OptixHandler;

/// \brief Holds the samples that an \c OptixHandler renders.
///
/// The samples live in device memory.
/// The resolution and the channels are fixed when the buffer is built.
///
/// Public operations do not need a current CUDA context.
/// They leave the current CUDA context of the calling thread unchanged.
///
/// For every pixel, each channel holds the mean of the samples in the buffer.
///
/// \warning Destroy the buffer before the handler that built it.
/// \remark A buffer is not thread-safe.
class OptixAccumulationBuffer final : private Noncopyable {
public:
    /// \brief Creates a buffer from \p spec.
    ///
    /// The buffer has no samples.
    ///
    /// \pre Both dimensions of the resolution of \p spec are nonzero.
    /// \pre The channels of \p spec have no unknown bit.
    /// \throw kira::Anyhow If CUDA fails.
    OptixAccumulationBuffer(OptixHandler &handler, FilmSpec const &spec);

    ~OptixAccumulationBuffer();

    /// \brief Removes all samples.
    ///
    /// The next render starts a new accumulation.
    void clear() noexcept;

    /// \brief Returns the number of samples per pixel in the buffer.
    [[nodiscard]] std::uint64_t getSampleCount() const noexcept;

    /// \brief Returns the spec that the buffer was built from.
    [[nodiscard]] FilmSpec getSpec() const noexcept;

    /// \brief Copies the mean of the samples into \p film.
    ///
    /// Waits until the copy is done.
    ///
    /// \warning If the copy fails, the values in \p film are unspecified.
    ///
    /// \pre \p film has the resolution and the channels of the buffer.
    /// \throw kira::Anyhow If CUDA fails.
    void download(Film &film);

private:
    friend class OptixHandler;

    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux
