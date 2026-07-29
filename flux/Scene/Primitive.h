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
class Geometry;
struct PreliminaryIntersection;
struct SurfaceInteraction;

/// \brief Places Geometry in a Context.
///
/// The \c geometry_ctx_id property identifies Geometry in the same Context.
/// The Primitive owns its instance transform, while the Context owns the
/// Geometry. A backend scene maps the stable Context ID to a dense geometry
/// index.
class Primitive final : public RenderObject {
    friend class TXContext;

public:
    /// \brief Stores primitive indices for a backend scene.
    struct Impl;

    /// \brief Returns the context ID of the bound geometry.
    [[nodiscard]] std::size_t getGeometryContextId() const noexcept { return geometryContextId_; }

    /// \brief Resolves and returns the bound geometry.
    ///
    /// \throw std::out_of_range if the geometry no longer exists.
    /// \throw kira::Anyhow if the ID does not identify Geometry or this
    /// primitive no longer belongs to a context.
    [[nodiscard]] Ref<Geometry const> getGeometry() const;

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

/// \brief Stores primitive indices for a backend scene.
///
/// Both indices address tables in that scene.
struct Primitive::Impl {
    /// Sentinel used when this primitive has no BSDF.
    static constexpr std::uint32_t invalidBSDFIndex = std::numeric_limits<std::uint32_t>::max();

    /// Dense index of the bound geometry.
    std::uint32_t geometryIndex{};

    /// Dense index of the bound BSDF, or \c invalidBSDFIndex.
    std::uint32_t bsdfIndex{invalidBSDFIndex};

public:
    /// \brief Returns the dense geometry index in this backend scene.
    [[nodiscard]] KIRA_HOST_DEVICE inline std::uint32_t getGeometryIndex() const noexcept;

    /// \brief Returns whether this primitive has a BSDF.
    [[nodiscard]] KIRA_HOST_DEVICE inline bool hasBSDF() const noexcept;

    /// \brief Returns the dense BSDF index in this backend scene.
    ///
    /// \pre \c hasBSDF() is true.
    [[nodiscard]] KIRA_HOST_DEVICE inline std::uint32_t getBSDFIndex() const noexcept;
};

static_assert(std::is_standard_layout_v<Primitive::Impl>);
static_assert(std::is_trivially_copyable_v<Primitive::Impl>);

namespace optix {
/// OptiX alias for Primitive::Impl.
using Primitive = ::flux::Primitive::Impl;
} // namespace optix
} // namespace flux
