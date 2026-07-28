#pragma once

#include <cstdint>
#include <cuda/std/variant>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Identifies a concrete BSDF implementation.
enum class BSDFType : std::uint8_t {
    Diffuse,
    Count,
};

/// \brief Identifies the lobe represented by a BSDF sample.
enum class BSDFLobe : std::uint8_t {
    DiffuseReflection,
};

/// \brief Hit-local input shared by BSDF evaluation and sampling.
///
/// Both directions used with this query point away from the surface. \c wo
/// points toward the previous path vertex.
struct BSDFQuery {
    SurfaceInteraction const &surface;
    Vec3f wo;
};

/// \brief Joint result of BSDF evaluation and PDF calculation.
struct BSDFEvaluation {
    /// BSDF value without the cosine factor.
    Spectrum f{};

    /// Solid-angle PDF for the queried direction.
    float pdf{};
};

/// \brief Result of sampling one BSDF lobe.
struct BSDFSample {
    /// BSDF value without the cosine factor.
    Spectrum f{};

    /// Sampled direction toward the next path vertex.
    Vec3f wi{};

    /// Solid-angle PDF of \c wi.
    float pdf{};

    /// Relative index of refraction across the sampled interface.
    float eta{1.0F};

    /// Lobe selected by the sample.
    BSDFLobe lobe{BSDFLobe::DiffuseReflection};
};

/// \brief Supplies the common device-side BSDF interface.
///
/// A concrete implementation may provide either \c evaluateAndPdf_ or the
/// pair \c evaluate_ and \c pdf_. The mixin derives the other form without
/// introducing another runtime dispatch layer.
template <typename Derived> class BSDFMixin {
private:
    [[nodiscard]] KIRA_DEVICE Derived const &derived_() const noexcept {
        return *static_cast<Derived const *>(this);
    }

    [[nodiscard]] KIRA_DEVICE Derived &derived_() noexcept { return *static_cast<Derived *>(this); }

public:
    /// \brief Prepares state local to one surface hit.
    KIRA_DEVICE void init(SurfaceInteraction &surface, Vec3f const &wo) noexcept {
        if constexpr (requires(Derived &bsdf) { bsdf.init_(surface, wo); })
            derived_().init_(surface, wo);
    }

    /// \brief Evaluates the BSDF without the cosine factor.
    [[nodiscard]] KIRA_DEVICE Spectrum
    evaluate(BSDFQuery const &query, Vec3f const &wi) const noexcept {
        if constexpr (requires(Derived const &bsdf) { bsdf.evaluate_(query, wi); })
            return derived_().evaluate_(query, wi);
        else
            return derived_().evaluateAndPdf_(query, wi).f;
    }

    /// \brief Returns the solid-angle PDF for \p wi.
    [[nodiscard]] KIRA_DEVICE float pdf(BSDFQuery const &query, Vec3f const &wi) const noexcept {
        if constexpr (requires(Derived const &bsdf) { bsdf.pdf_(query, wi); })
            return derived_().pdf_(query, wi);
        else
            return derived_().evaluateAndPdf_(query, wi).pdf;
    }

    /// \brief Jointly evaluates the BSDF and its sampling PDF.
    [[nodiscard]] KIRA_DEVICE BSDFEvaluation
    evaluateAndPdf(BSDFQuery const &query, Vec3f const &wi) const noexcept {
        if constexpr (requires(Derived const &bsdf) { bsdf.evaluateAndPdf_(query, wi); })
            return derived_().evaluateAndPdf_(query, wi);
        else
            return {
                .f = derived_().evaluate_(query, wi),
                .pdf = derived_().pdf_(query, wi),
            };
    }

    /// \brief Samples the concrete BSDF.
    [[nodiscard]] KIRA_DEVICE BSDFSample
    sample(BSDFQuery const &query, float lobeSample, Vec2f const &directionSample) const noexcept {
        return derived_().sample_(query, lobeSample, directionSample);
    }
};

/// \brief Host-side base for surface scattering models.
class BSDF : public RenderObject {
protected:
    /// \brief Creates a BSDF of \p type in \p tx.
    BSDF(TXContext &tx, kira::Properties properties, BSDFType type);

public:
    /// \brief Heterogeneous representation stored in a device scene.
    struct DeviceImpl;

    /// \brief Returns the concrete implementation type.
    [[nodiscard]] BSDFType getType() const noexcept { return type_; }

    /// \brief Builds this BSDF's device payload.
    [[nodiscard]] DeviceImpl getDeviceImpl() const;

private:
    BSDFType type_;
};

/// \brief Lambertian reflection with constant reflectance.
///
/// \par Properties
/// - \c reflectance: optional RGB value in \f$[0,1]^3\f$; defaults to 0.5.
class DiffuseBSDF final : public BSDF {
    friend class TXContext;

public:
    /// \brief Device-side Lambertian implementation.
    struct DeviceImpl;

    /// \brief Returns the constant surface reflectance.
    [[nodiscard]] Spectrum const &getReflectance() const noexcept { return reflectance_; }

    /// \brief Builds the constant device payload.
    [[nodiscard]] DeviceImpl getDeviceImpl() const noexcept;

private:
    DiffuseBSDF(TXContext &tx, kira::Properties properties);

    Spectrum reflectance_{0.5F, 0.5F, 0.5F};
};

/// \brief Device-side Lambertian reflection.
struct DiffuseBSDF::DeviceImpl : BSDFMixin<DeviceImpl> {
    /// Constant RGB reflectance.
    Spectrum reflectance;

public:
    /// \brief Samples a cosine-weighted reflection direction.
    ///
    /// \param lobeSample Reserved for selecting a lobe in multi-lobe BSDFs.
    /// \param directionSample Uniform sample in the unit square.
    [[nodiscard]] KIRA_DEVICE BSDFSample
    sample_(BSDFQuery const &query, float lobeSample, Vec2f const &directionSample) const noexcept;

    /// \brief Evaluates the BSDF and sampling PDF for \p wi.
    [[nodiscard]] KIRA_DEVICE BSDFEvaluation
    evaluateAndPdf_(BSDFQuery const &query, Vec3f const &wi) const noexcept;
};

/// \brief Heterogeneous BSDF payload stored in one device-scene table.
struct BSDF::DeviceImpl : cuda::std::variant<DiffuseBSDF::DeviceImpl> {
    using Base = cuda::std::variant<DiffuseBSDF::DeviceImpl>;
    using Base::Base;

public:
    /// \brief Returns the concrete payload selected by the SBT program.
    ///
    /// \pre \c T matches the concrete program handling this surface.
    template <typename T> [[nodiscard]] KIRA_HOST_DEVICE T const &get() const noexcept {
        return cuda::std::get<T>(static_cast<Base const &>(*this));
    }
};

static_assert(std::is_standard_layout_v<DiffuseBSDF::DeviceImpl>);
static_assert(std::is_trivially_copyable_v<DiffuseBSDF::DeviceImpl>);
static_assert(std::is_trivially_copyable_v<BSDF::DeviceImpl>);
} // namespace flux
