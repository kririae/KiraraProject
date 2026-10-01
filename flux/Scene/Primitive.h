#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

#include "flux/Core/EnumFlags.h"
#include "flux/Scene/RenderObject.h"
#include "kira/Compiler.h"

namespace flux {
class BSDF;
class EDF;
class Geometry;
struct PreliminaryIntersection;
struct SurfaceInteraction;

/// \brief Places geometry and its shading models in a scene.
///
/// \par Properties
/// - \c transform: optional row-major 3x4 geometry-to-world matrix.
/// - \c geometry_ctx_id binds existing Geometry. Otherwise the properties
///   create Geometry inline.
/// - \c bsdf_ctx_id binds an existing BSDF. An inline \c bsdf table creates one
///   in the same transaction.
/// - \c edf_ctx_id binds an existing EDF. An inline \c edf table creates one
///   in the same transaction.
///
/// Scene loaders translate symbolic references to context IDs before creation.
class Primitive final : public RenderObject {
    friend class TXContext;

public:
    struct Impl;

    /// \brief Properties of a primitive that a setter can change.
    ///
    /// One bit per setter. A setter records its bit only when the value changes.
    enum class DirtyBits : std::uint32_t {
        None = 0,
        Geometry = 1U << 0U,
        BSDF = 1U << 1U,
        EDF = 1U << 2U,
        Transform = 1U << 3U,
        Visibility = 1U << 4U,
    };

    /// Kind whose index map holds primitives.
    static constexpr IndexedKind indexedKind = IndexedKind::Primitive;

    ~Primitive() override;

    [[nodiscard]] std::optional<IndexedKind> getIndexedKind() const noexcept override {
        return indexedKind;
    }

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

    /// \brief Returns the bound EDF, or an empty reference.
    [[nodiscard]] Ref<EDF const> getEDF() const noexcept;

    /// \brief Replaces the EDF binding with one from the same Context.
    ///
    /// An empty reference removes the binding.
    void setEDF(Ref<EDF const> edf);

    /// \brief Returns the row-major object-to-world affine transform.
    [[nodiscard]] std::array<float, 12> const &getTransform() const noexcept { return transform_; }

    /// \brief Replaces the object-to-world affine transform.
    void setTransform(std::array<float, 12> const &transform) {
        setIfDifferent(transform_, transform, DirtyBits::Transform);
    }

    /// \brief Returns whether this primitive participates in rendering.
    [[nodiscard]] bool isVisible() const noexcept { return visible_; }

    /// \brief Includes or excludes this primitive from rendering.
    void setVisible(bool visible) { setIfDifferent(visible_, visible, DirtyBits::Visibility); }

    /// \brief Returns whether this primitive is a light.
    ///
    /// A light is a visible primitive with an EDF.
    [[nodiscard]] bool isLight() const noexcept { return visible_ && edf_; }

    /// \brief Returns this primitive's table entry, or a hole when it is hidden.
    ///
    /// Reads the owning Context, so call it only during sync. The entry depends
    /// only on this primitive and the indices that Context assigned, never on
    /// another table.
    [[nodiscard]] Impl getImpl() const;

    /// \brief Estimates the object-to-world surface-area scale.
    [[nodiscard]] float estimateAreaScale() const noexcept;

    [[nodiscard]] bool hasNonUniformScale() const noexcept;

    /// \brief Estimates total emitted power for light selection.
    [[nodiscard]] float estimatePower() const noexcept;

private:
    Primitive(TXContext &tx, kira::Properties const &props);

    Ref<Geometry const> geometry_;
    Ref<BSDF const> bsdf_;
    Ref<EDF const> edf_;
    std::array<float, 12> transform_{
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
    };
    bool visible_{true};
};

/// \brief Links a primitive to its geometry and shading models.
struct Primitive::Impl {
    /// Sentinel used when this primitive has no BSDF.
    static constexpr std::uint32_t invalidBSDFIndex = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint32_t invalidEDFIndex = std::numeric_limits<std::uint32_t>::max();

    /// Sentinel used when no visible primitive has this entry's index.
    static constexpr std::uint32_t invalidGeometryIndex = std::numeric_limits<std::uint32_t>::max();

    /// Geometry index assigned by Context, or \c invalidGeometryIndex at a hole.
    std::uint32_t geometryIndex{invalidGeometryIndex};

    /// BSDF index assigned by Context, or \c invalidBSDFIndex.
    std::uint32_t bsdfIndex{invalidBSDFIndex};

    /// EDF index assigned by Context, or \c invalidEDFIndex.
    std::uint32_t edfIndex{invalidEDFIndex};

public:
    /// \brief Returns whether the entry has no visible primitive.
    ///
    /// A default entry is a hole. Rendering never reads a hole.
    [[nodiscard]] KIRA_HOST_DEVICE bool isHole() const noexcept {
        return geometryIndex == invalidGeometryIndex;
    }

    /// \brief Returns the geometry index assigned by Context.
    ///
    /// \pre \c isHole() is false.
    [[nodiscard]] KIRA_HOST_DEVICE std::uint32_t getGeometryIndex() const noexcept {
        return geometryIndex;
    }

    /// \brief Returns whether this primitive has a BSDF.
    [[nodiscard]] KIRA_HOST_DEVICE bool hasBSDF() const noexcept {
        return bsdfIndex != invalidBSDFIndex;
    }

    /// \brief Returns the bound BSDF index assigned by Context.
    ///
    /// \pre \c hasBSDF() is true.
    [[nodiscard]] KIRA_HOST_DEVICE std::uint32_t getBSDFIndex() const noexcept { return bsdfIndex; }

    [[nodiscard]] KIRA_HOST_DEVICE bool hasEDF() const noexcept {
        return edfIndex != invalidEDFIndex;
    }

    [[nodiscard]] KIRA_HOST_DEVICE std::uint32_t getEDFIndex() const noexcept { return edfIndex; }
};

template <> inline constexpr bool isEnumFlags<Primitive::DirtyBits> = true;

static_assert(std::is_standard_layout_v<Primitive::Impl>);
static_assert(std::is_trivially_copyable_v<Primitive::Impl>);

namespace optix {
/// OptiX alias for Primitive::Impl.
using Primitive = ::flux::Primitive::Impl;
} // namespace optix
} // namespace flux
