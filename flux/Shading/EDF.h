#pragma once

#include <cuda/std/variant>
#include <type_traits>
#include <utility>

#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief World-space surface orientation used to evaluate emission.
///
/// \c wo points away from the surface in world space.
struct EDFQuery {
    Vec3f geometricNormal{};
    Vec3f wo{};
};

/// \brief Host-side base for surface emission models.
class EDF : public RenderObject {
    friend class TXContext;

public:
    struct Impl;

    [[nodiscard]] virtual Impl getImpl() const = 0;
    /// \brief Estimates emitted luminance for light selection.
    [[nodiscard]] virtual float estimateLuminance() const noexcept = 0;

protected:
    explicit EDF(TXContext &tx) : RenderObject(tx) {}

private:
    [[nodiscard]] static Ref<EDF> create(TXContext &tx, kira::Properties const &props);
};

/// \brief One-sided constant radiance emission.
///
/// \par Properties
/// - \c radiance: optional nonnegative RGB radiance; defaults to one.
class ConstantEDF final : public EDF {
    friend class TXContext;

public:
    struct Impl;

    [[nodiscard]] Spectrum const &getRadiance() const noexcept { return radiance_; }
    void setRadiance(Spectrum const &radiance);

    [[nodiscard]] EDF::Impl getImpl() const override;
    [[nodiscard]] float estimateLuminance() const noexcept override;

private:
    ConstantEDF(TXContext &tx, kira::Properties const &props);

    Spectrum radiance_{1.0F, 1.0F, 1.0F};
};

struct ConstantEDF::Impl {
    Spectrum radiance;

    [[nodiscard]] KIRA_HOST_DEVICE Spectrum evaluate(EDFQuery const &query) const noexcept {
        return query.geometricNormal.dot(query.wo) > 0.0F ? radiance : Spectrum{};
    }
};

struct EDF::Impl : cuda::std::variant<ConstantEDF::Impl> {
    using Base = cuda::std::variant<ConstantEDF::Impl>;
    using Base::Base;

    [[nodiscard]] KIRA_HOST_DEVICE Spectrum evaluate(EDFQuery const &query) const noexcept {
        return cuda::std::visit([&](auto const &edf) {
            return edf.evaluate(query);
        }, static_cast<Base const &>(*this));
    }
};

static_assert(std::is_standard_layout_v<ConstantEDF::Impl>);
static_assert(std::is_trivially_copyable_v<ConstantEDF::Impl>);
static_assert(std::is_trivially_copyable_v<EDF::Impl>);
} // namespace flux
