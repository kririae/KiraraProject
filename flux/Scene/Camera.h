#pragma once

#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Core/Ray.h"
#include "flux/Scene/RenderObject.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Pinhole camera selected for a render launch.
///
/// \par Properties
/// - \c position: optional Vec3f camera position; defaults to \f$(0,0,0)\f$.
/// - \c look_at: optional Vec3f target; defaults to \f$(0,0,-1)\f$.
/// - \c ref_up: optional Vec3f reference up; defaults to \f$(0,1,0)\f$.
/// - \c fov: optional float vertical field of view in degrees; defaults to 60.
class Camera final : public RenderObject {
    friend class TXContext;

public:
    /// \brief Device implementation of the pinhole camera.
    struct DeviceImpl;

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

    /// \brief Materializes the current pinhole camera for a launch.
    ///
    /// \throw kira::Anyhow If the camera frame is non-finite or degenerate.
    [[nodiscard]] DeviceImpl getDeviceImpl() const;

private:
    Camera(TXContext &tx, kira::Properties properties);

    Vec3f position_{0.0F, 0.0F, 0.0F};
    Vec3f lookAt_{0.0F, 0.0F, -1.0F};
    Vec3f referenceUp_{0.0F, 1.0F, 0.0F};
    float verticalFieldOfView_{60.0F};
};

/// \brief Device implementation of a pinhole camera.
struct Camera::DeviceImpl {
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

public:
    /// \brief Generates the primary ray through pixel \p x, \p y.
    ///
    /// \pre \p width and \p height are nonzero, and the pixel coordinates are
    /// in range.
    [[nodiscard]] KIRA_DEVICE inline Ray generateRay(
        std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height
    ) const noexcept;
};

static_assert(std::is_standard_layout_v<Camera::DeviceImpl>);
static_assert(std::is_trivially_copyable_v<Camera::DeviceImpl>);

namespace optix {
/// Device representation of a pinhole camera.
using Camera = ::flux::Camera::DeviceImpl;
} // namespace optix
} // namespace flux
