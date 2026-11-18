#pragma once

#include <memory>

#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "flux/Scene/Film.h"

namespace flux {
class OptixDeviceSession;

/// \brief Maps the color of a film, guided by its albedo and normal, to its denoised color.
///
/// The denoiser reads the normal as the film holds it, in world space.
///
/// Public operations do not need a current CUDA context.
/// They leave the current CUDA context of the calling thread unchanged.
///
/// \warning Destroy the denoiser before its session.
/// \remark A denoiser is not thread-safe.
class OptixFilmDenoiser final : private Noncopyable {
public:
    /// \brief Creates a denoiser for films of the given resolution.
    ///
    /// \pre Both dimensions of the resolution are nonzero.
    /// \throw kira::Anyhow If CUDA or OptiX fails.
    OptixFilmDenoiser(OptixDeviceSession const &session, Vec2u resolution);

    ~OptixFilmDenoiser();

    [[nodiscard]] Vec2u getResolution() const noexcept;

    /// \brief Writes the denoised color of a film from its color, albedo, and normal.
    ///
    /// Queues the work on the stream of the session, and does not wait.
    ///
    /// \pre The channels of the film are device memory that work on the stream of the session may
    /// read and write.
    /// \throw std::invalid_argument If the film lacks color, albedo, normal, or denoised color, or
    /// differs in resolution from the denoiser.
    /// \throw kira::Anyhow If OptiX fails.
    void denoise(Film::Impl const &film);

private:
    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux
