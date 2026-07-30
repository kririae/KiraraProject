#pragma once

#include <cstdint>
#include <cuda/std/variant>
#include <type_traits>
#include <utility>

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

/// \brief Supplies the common concrete BSDF interface.
///
/// A concrete implementation may provide either \c evaluateAndPdf_ or the
/// pair \c evaluate_ and \c pdf_. The mixin derives the other form without
/// introducing another runtime dispatch layer.
template <typename Derived> class BSDFMixin {
private:
    [[nodiscard]] KIRA_HOST_DEVICE Derived const &derived_() const noexcept {
        return *static_cast<Derived const *>(this);
    }

    [[nodiscard]] KIRA_HOST_DEVICE Derived &derived_() noexcept {
        return *static_cast<Derived *>(this);
    }

public:
    /// \brief Prepares state local to one surface hit.
    KIRA_HOST_DEVICE void init(SurfaceInteraction &surface, Vec3f const &wo) noexcept {
        if constexpr (requires(Derived &bsdf) { bsdf.init_(surface, wo); })
            derived_().init_(surface, wo);
    }

    /// \brief Evaluates the BSDF without the cosine factor.
    [[nodiscard]] KIRA_HOST_DEVICE Spectrum
    evaluate(BSDFQuery const &query, Vec3f const &wi) const noexcept {
        if constexpr (requires(Derived const &bsdf) { bsdf.evaluate_(query, wi); })
            return derived_().evaluate_(query, wi);
        else
            return derived_().evaluateAndPdf_(query, wi).f;
    }

    /// \brief Returns the solid-angle PDF for \p wi.
    [[nodiscard]] KIRA_HOST_DEVICE float
    pdf(BSDFQuery const &query, Vec3f const &wi) const noexcept {
        if constexpr (requires(Derived const &bsdf) { bsdf.pdf_(query, wi); })
            return derived_().pdf_(query, wi);
        else
            return derived_().evaluateAndPdf_(query, wi).pdf;
    }

    /// \brief Jointly evaluates the BSDF and its sampling PDF.
    [[nodiscard]] KIRA_HOST_DEVICE BSDFEvaluation
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
    [[nodiscard]] KIRA_HOST_DEVICE BSDFSample
    sample(BSDFQuery const &query, float lobeSample, Vec2f const &directionSample) const noexcept {
        return derived_().sample_(query, lobeSample, directionSample);
    }
};

/// \brief Host-side base for surface scattering models.
class BSDF : public RenderObject {
    friend class TXContext;

protected:
    /// \brief Creates a BSDF of \p type in \p tx.
    BSDF(TXContext &tx, BSDFType type);

public:
    /// \brief Heterogeneous scattering implementation.
    struct Impl;

    /// \brief Returns the concrete implementation type.
    [[nodiscard]] BSDFType getType() const noexcept { return type_; }

    /// \brief Builds this BSDF's scattering implementation.
    [[nodiscard]] Impl getImpl() const;

private:
    [[nodiscard]] static Ref<BSDF> create(TXContext &tx, kira::Properties const &props);

    BSDFType type_;
};

/// \brief Lambertian reflection with constant reflectance.
///
/// \par Properties
/// - \c R: optional RGB reflectance in \f$[0,1]^3\f$; defaults to 0.5.
class DiffuseBSDF final : public BSDF {
    friend class TXContext;

public:
    /// \brief Lambertian scattering implementation.
    struct Impl;

    /// \brief Returns the constant surface reflectance.
    [[nodiscard]] Spectrum const &getReflectance() const noexcept { return reflectance_; }

    /// \brief Builds the constant scattering implementation.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    DiffuseBSDF(TXContext &tx, kira::Properties const &props);

    Spectrum reflectance_{0.5F, 0.5F, 0.5F};
};

/// \brief Lambertian reflection implementation.
struct DiffuseBSDF::Impl : BSDFMixin<Impl> {
    /// Constant RGB reflectance.
    Spectrum reflectance;

public:
    /// \brief Samples a cosine-weighted reflection direction.
    ///
    /// \param lobeSample Reserved for selecting a lobe in multi-lobe BSDFs.
    /// \param directionSample Uniform sample in the unit square.
    [[nodiscard]] KIRA_HOST_DEVICE BSDFSample
    sample_(BSDFQuery const &query, float lobeSample, Vec2f const &directionSample) const noexcept;

    /// \brief Evaluates the BSDF and sampling PDF for \p wi.
    [[nodiscard]] KIRA_HOST_DEVICE BSDFEvaluation
    evaluateAndPdf_(BSDFQuery const &query, Vec3f const &wi) const noexcept;
};

/// \brief Heterogeneous scattering implementation.
///
/// Generic callers use the common BSDF operations below. A caller that
/// already knows the concrete type, such as an OptiX hit program selected by
/// the SBT, may use \c get to bypass dynamic dispatch.
struct BSDF::Impl : cuda::std::variant<DiffuseBSDF::Impl> {
    using Base = cuda::std::variant<DiffuseBSDF::Impl>;
    using Base::Base;

private:
    template <typename Function>
    KIRA_HOST_DEVICE decltype(auto) dispatch(Function &&function) noexcept {
        return cuda::std::visit(std::forward<Function>(function), static_cast<Base &>(*this));
    }

    template <typename Function>
    KIRA_HOST_DEVICE decltype(auto) dispatch(Function &&function) const noexcept {
        return cuda::std::visit(std::forward<Function>(function), static_cast<Base const &>(*this));
    }

public:
    /// \brief Prepares state local to one surface hit.
    KIRA_HOST_DEVICE void init(SurfaceInteraction &surface, Vec3f const &wo) noexcept {
        dispatch([&](auto &bsdf) { bsdf.init(surface, wo); });
    }

    /// \brief Evaluates the BSDF without the cosine factor.
    [[nodiscard]] KIRA_HOST_DEVICE Spectrum
    evaluate(BSDFQuery const &query, Vec3f const &wi) const noexcept {
        return dispatch([&](auto const &bsdf) { return bsdf.evaluate(query, wi); });
    }

    /// \brief Returns the solid-angle PDF for \p wi.
    [[nodiscard]] KIRA_HOST_DEVICE float
    pdf(BSDFQuery const &query, Vec3f const &wi) const noexcept {
        return dispatch([&](auto const &bsdf) { return bsdf.pdf(query, wi); });
    }

    /// \brief Jointly evaluates the BSDF and its sampling PDF.
    [[nodiscard]] KIRA_HOST_DEVICE BSDFEvaluation
    evaluateAndPdf(BSDFQuery const &query, Vec3f const &wi) const noexcept {
        return dispatch([&](auto const &bsdf) { return bsdf.evaluateAndPdf(query, wi); });
    }

    /// \brief Samples the selected BSDF implementation.
    [[nodiscard]] KIRA_HOST_DEVICE BSDFSample
    sample(BSDFQuery const &query, float lobeSample, Vec2f const &directionSample) const noexcept {
        return dispatch([&](auto const &bsdf) {
            return bsdf.sample(query, lobeSample, directionSample);
        });
    }

    /// \brief Returns the selected concrete implementation.
    ///
    /// \pre \c T matches the concrete implementation held by this object.
    template <typename T> [[nodiscard]] KIRA_HOST_DEVICE T const &get() const noexcept {
        return cuda::std::get<T>(static_cast<Base const &>(*this));
    }
};

static_assert(std::is_standard_layout_v<DiffuseBSDF::Impl>);
static_assert(std::is_trivially_copyable_v<DiffuseBSDF::Impl>);
static_assert(std::is_trivially_copyable_v<BSDF::Impl>);
} // namespace flux
