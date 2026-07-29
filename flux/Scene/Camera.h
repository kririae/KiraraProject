#pragma once

#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "flux/Core/Ray.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Perspective camera selected by a render product.
///
/// \par Properties
/// - \c position: optional Vec3f camera position; defaults to \f$(0,0,0)\f$.
/// - \c look_at: optional Vec3f target; defaults to \f$(0,0,-1)\f$.
/// - \c ref_up: optional Vec3f reference up; defaults to \f$(0,1,0)\f$.
/// - \c fov: optional float vertical field of view in degrees; defaults to 60.
/// - \c lens_radius: optional nonnegative float lens radius; defaults to 0.
/// - \c focal_distance: optional nonnegative float focus-plane distance;
///   defaults to 0 and must be positive when the lens radius is positive.
class Camera final : public Object {
public:
    struct Impl;

    /// \brief Creates a camera from \p properties.
    [[nodiscard]] static Ref<Camera> create(kira::Properties properties = {});

    /// \brief Returns the camera position in world space.
    [[nodiscard]] Vec3f const &getPosition() const noexcept { return position_; }

    /// \brief Sets the camera position in world space.
    void setPosition(Vec3f const &position) noexcept { position_ = position; }

    /// \brief Returns the world-space point observed by the camera.
    [[nodiscard]] Vec3f const &getLookAt() const noexcept { return lookAt_; }

    /// \brief Sets the world-space point observed by the camera.
    void setLookAt(Vec3f const &lookAt) noexcept { lookAt_ = lookAt; }

    /// \brief Returns the reference up direction.
    [[nodiscard]] Vec3f const &getReferenceUp() const noexcept { return referenceUp_; }

    /// \brief Sets the reference up direction.
    void setReferenceUp(Vec3f const &referenceUp) noexcept { referenceUp_ = referenceUp; }

    /// \brief Returns the vertical field of view in degrees.
    [[nodiscard]] float getVerticalFieldOfView() const noexcept { return verticalFieldOfView_; }

    /// \brief Sets the vertical field of view in degrees.
    ///
    /// \throw kira::Anyhow If \p degrees is outside the open interval
    /// \f$(0, 180)\f$.
    void setVerticalFieldOfView(float degrees);

    /// \brief Returns the lens radius in world-space units.
    [[nodiscard]] float getLensRadius() const noexcept { return lensRadius_; }

    /// \brief Sets the lens radius.
    ///
    /// \throw kira::Anyhow If \p radius is negative or non-finite, or if a
    /// positive radius has no positive focal distance.
    void setLensRadius(float radius);

    [[nodiscard]] float getFocalDistance() const noexcept { return focalDistance_; }
    void setFocalDistance(float distance);

    ///
    [[nodiscard]] Impl getImpl() const;

private:
    explicit Camera(kira::Properties properties);

    Vec3f position_{0.0F, 0.0F, 0.0F};
    Vec3f lookAt_{0.0F, 0.0F, -1.0F};
    Vec3f referenceUp_{0.0F, 1.0F, 0.0F};
    float verticalFieldOfView_{60.0F};
    float lensRadius_{};
    float focalDistance_{};
};

/// \brief Compact implementation of a perspective camera.
struct Camera::Impl {
    /// Lens position in world space.
    Vec3f position{};

    /// Normalized viewing direction.
    Vec3f forward{};

    /// Normalized horizontal camera axis.
    Vec3f right{};

    /// Normalized vertical camera axis.
    Vec3f up{};

    /// Half-height of the image plane at unit distance.
    float halfHeight{};

    /// Radius of the thin lens, or zero for a pinhole camera.
    float lensRadius{};

    /// Distance from the lens center to the focus plane.
    float focalDistance{};

public:
    /// \brief Generates the primary ray through \p rasterPosition.
    ///
    /// \param lensSample Uniform sample in \f$[0,1)^2\f$ used by the thin
    /// lens.
    /// \pre Both resolution components are nonzero, and \p rasterPosition is
    /// inside the image's half-open bounds.
    [[nodiscard]] KIRA_HOST_DEVICE inline Ray generateRay(
        Vec2f const &rasterPosition, Vec2f const &lensSample, Vec2u const &resolution
    ) const noexcept;

    /// \brief Compares the complete ray-generation payload.
    [[nodiscard]] bool operator==(Impl const &) const = default;
};

static_assert(std::is_standard_layout_v<Camera::Impl>);
static_assert(std::is_trivially_copyable_v<Camera::Impl>);

namespace optix {
/// Device representation of a perspective camera.
using Camera = ::flux::Camera::Impl;
} // namespace optix
} // namespace flux
