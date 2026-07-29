#pragma once

#include <cstdint>

#include "flux/Core/Object.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Film.h"

namespace flux {
/// \brief Persistent render target selected for a launch.
///
/// The object owns its film description and retains the camera used to render
/// it. Renderer backends keep device storage separately.
///
/// \par Properties
/// - \c width: required nonzero uint32 image width.
/// - \c height: required nonzero uint32 image height.
/// - \c num_samples: optional nonzero uint32 target sample count; defaults to 1.
class RenderProduct final : public Object {
public:
    /// \brief Creates a render product bound to \p camera.
    ///
    /// \p camera must be nonnull.
    [[nodiscard]] static Ref<RenderProduct>
    create(Ref<Camera const> camera, kira::Properties properties = {});

    [[nodiscard]] Camera const &getCamera() const noexcept { return *camera_; }

    /// \brief Selects the camera used by later renders.
    ///
    /// Existing renderer accumulation becomes stale when the resulting
    /// \c Camera::Impl changes. \p camera must be nonnull.
    void setCamera(Ref<Camera const> camera);

    [[nodiscard]] Film const &getFilm() const noexcept { return film_; }
    [[nodiscard]] Film &getFilm() noexcept { return film_; }

    [[nodiscard]] std::uint32_t getSamplesPerPixel() const noexcept { return samplesPerPixel_; }

    /// \brief Changes the target number of samples per pixel.
    ///
    /// Changing the target keeps samples already accumulated by a renderer.
    /// \p samples must be nonzero.
    void setSamplesPerPixel(std::uint32_t samples);

private:
    RenderProduct(Ref<Camera const> camera, kira::Properties properties);

    Film film_;
    Ref<Camera const> camera_;
    std::uint32_t samplesPerPixel_;
};
} // namespace flux
