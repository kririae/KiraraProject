#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "flux/Scene/RenderObject.h"
#include "kira/Compiler.h"

namespace flux {
class BSDF;
class Geometry;
struct PreliminaryIntersection;
struct SurfaceInteraction;

/// \brief Places Geometry and its shading state in a scene.
///
/// \par Properties
/// - \c geometry_ctx_id binds existing Geometry. Otherwise the properties
///   create Geometry inline.
/// - \c bsdf_ctx_id binds an existing BSDF. An inline \c bsdf table creates one
///   in the same transaction.
///
/// Scene loaders translate symbolic references to context IDs before creation.
class Primitive final : public RenderObject {
    friend class TXContext;

public:
    struct Impl;

    ~Primitive() override;

    /// \brief Returns the bound geometry.
    [[nodiscard]] Ref<Geometry const> getGeometry() const noexcept;

    /// \brief Replaces the geometry with one from the same Context.
    void setGeometry(Ref<Geometry const> geometry);

    /// \brief Returns the bound BSDF, or an empty reference.
    [[nodiscard]] Ref<BSDF const> getBSDF() const noexcept;

    /// \brief Replaces the BSDF binding with one from the same Context.
    ///
    /// An empty reference removes the binding.
    void setBSDF(Ref<BSDF const> bsdf);

    /// \brief Returns the row-major object-to-world affine transform.
    [[nodiscard]] std::array<float, 12> const &getTransform() const noexcept { return transform_; }

    /// \brief Replaces the object-to-world affine transform.
    void setTransform(std::array<float, 12> const &transform) noexcept { transform_ = transform; }

    /// \brief Returns whether this primitive participates in rendering.
    [[nodiscard]] bool isVisible() const noexcept { return visible_; }

    /// \brief Includes or excludes this primitive from rendering.
    void setVisible(bool visible) noexcept { visible_ = visible; }

private:
    Primitive(TXContext &tx, kira::Properties const &props);

    Ref<Geometry const> geometry_;
    Ref<BSDF const> bsdf_;
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
