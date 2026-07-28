#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

#include "flux/Scene/RenderObject.h"
#include "kira/Compiler.h"

namespace flux {
class BSDF;
class TriangleMesh;

/// \brief Places a triangle mesh in the host scene.
///
/// The \c geometry_ctx_id property identifies a TriangleMesh in the same
/// context. A primitive owns an instance transform but does not duplicate the
/// context's geometry ownership. Its stable context ID is not used as a device
/// array index.
class Primitive final : public RenderObject {
    friend class TXContext;

public:
    /// \brief Compact representation consumed by device programs.
    struct DeviceImpl;

    /// \brief Returns the context ID of the bound triangle mesh.
    [[nodiscard]] std::size_t getGeometryContextId() const noexcept { return geometryContextId_; }

    /// \brief Resolves and returns the bound triangle mesh.
    ///
    /// \throw std::out_of_range if the geometry no longer exists.
    /// \throw kira::Anyhow if the ID does not identify a TriangleMesh or this
    /// primitive no longer belongs to a context.
    [[nodiscard]] Ref<TriangleMesh const> getGeometry() const;

    /// \brief Returns the context ID of the bound BSDF, if present.
    [[nodiscard]] std::optional<std::size_t> getBSDFContextId() const noexcept {
        return bsdfContextId_;
    }

    /// \brief Resolves and returns the bound BSDF.
    ///
    /// A primitive without a BSDF returns an empty reference.
    /// \throw std::out_of_range if the BSDF no longer exists.
    /// \throw kira::Anyhow if the ID does not identify a BSDF or this
    /// primitive no longer belongs to a context.
    [[nodiscard]] Ref<BSDF const> getBSDF() const;

    /// \brief Returns the row-major object-to-world affine transform.
    [[nodiscard]] std::array<float, 12> const &getTransform() const noexcept { return transform_; }

    /// \brief Replaces the object-to-world affine transform.
    void setTransform(std::array<float, 12> const &transform) noexcept { transform_ = transform; }

    /// \brief Returns whether this primitive participates in rendering.
    [[nodiscard]] bool isVisible() const noexcept { return visible_; }

    /// \brief Includes or excludes this primitive from rendering.
    void setVisible(bool visible) noexcept { visible_ = visible; }

private:
    Primitive(TXContext &tx, kira::Properties properties);
    void link() override;

    std::size_t geometryContextId_;
    std::optional<std::size_t> bsdfContextId_;
    std::array<float, 12> transform_{
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
    };
    bool visible_{true};
};

/// \brief Device implementation of a scene primitive.
///
/// The index addresses the geometry table from the same OptiX context sync.
struct Primitive::DeviceImpl {
    /// Sentinel used when this primitive has no BSDF.
    static constexpr std::uint32_t invalidBSDFIndex = std::numeric_limits<std::uint32_t>::max();

    /// Dense index of the bound geometry.
    std::uint32_t geometryIndex{};

    /// Dense index of the bound BSDF, or \c invalidBSDFIndex.
    std::uint32_t bsdfIndex{invalidBSDFIndex};

public:
    /// \brief Returns the dense geometry index for this materialization.
    [[nodiscard]] KIRA_DEVICE inline std::uint32_t getGeometryIndex() const noexcept;

    /// \brief Returns whether this primitive has a BSDF.
    [[nodiscard]] KIRA_DEVICE inline bool hasBSDF() const noexcept;

    /// \brief Returns the dense BSDF index for this materialization.
    ///
    /// \pre \c hasBSDF() is true.
    [[nodiscard]] KIRA_DEVICE inline std::uint32_t getBSDFIndex() const noexcept;
};

static_assert(std::is_standard_layout_v<Primitive::DeviceImpl>);
static_assert(std::is_trivially_copyable_v<Primitive::DeviceImpl>);

namespace optix {
/// Device representation of a scene primitive.
using Primitive = ::flux::Primitive::DeviceImpl;
} // namespace optix
} // namespace flux
