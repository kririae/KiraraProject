#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Identifies a concrete sampler implementation.
enum class SamplerType : std::uint8_t {
    /// Independent pseudorandom sampling.
    Independent,
};

/// \brief Host-side sampling configuration selected by a context.
///
/// The first sampler successfully added to a context becomes active.
class Sampler : public RenderObject {
protected:
    Sampler(TXContext &tx, kira::Properties properties, SamplerType type);

    /// \copydoc ContextObject::registerTo
    void registerTo(TXContext &tx) override;

public:
    struct Impl;

    [[nodiscard]] SamplerType getType() const noexcept { return type_; }
    [[nodiscard]] Impl getImpl(Vec2u const &resolution) const;

private:
    SamplerType type_;
};

/// \brief Independent pseudorandom sampler.
class IndependentSampler final : public Sampler {
    friend class TXContext;

public:
    struct Impl;

    /// \brief Creates the concrete sampler for \p resolution.
    ///
    /// \param resolution Nonzero image resolution for the launch.
    [[nodiscard]] Impl getImpl(Vec2u const &resolution) const noexcept;

private:
    IndependentSampler(TXContext &tx, kira::Properties properties);
};

/// \brief Independent pseudorandom sampling implementation.
struct IndependentSampler::Impl {
    /// Current PCG state.
    std::uint64_t state;

    /// Odd PCG stream increment.
    std::uint64_t increment;

public:
    /// \brief Starts the sequence for one pixel sample.
    ///
    /// \param pixel Pixel coordinate within \p resolution.
    /// \param sampleIndex Zero-based sample index for the pixel.
    /// \param resolution Image resolution.
    /// \pre Both resolution components are nonzero and \p pixel is in range.
    KIRA_HOST_DEVICE inline void startPixelSample(
        Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
    ) noexcept;

    /// \brief Returns a uniformly distributed value in \f$[0,1)\f$.
    [[nodiscard]] KIRA_HOST_DEVICE inline float get1D() noexcept;

    /// \brief Returns two uniformly distributed values in \f$[0,1)^2\f$.
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec2f get2D() noexcept;

    /// \brief Returns a sample within the current pixel.
    ///
    /// The result lies in \f$[0,1)^2\f$.
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec2f getPixel2D() noexcept;
};

/// \brief Sampler dispatcher.
///
/// The explicit dispatch switch keeps the discriminator visible to OptiX bound-value
/// specialization.
struct Sampler::Impl {
    /// Concrete implementation selected for this dispatcher.
    SamplerType type;

    /// Concrete sampler storage selected by \c type.
    union Storage {
        /// Independent sampler state.
        IndependentSampler::Impl independent;
    } storage;

public:
    /// \copydoc IndependentSampler::Impl::startPixelSample
    KIRA_HOST_DEVICE inline void startPixelSample(
        Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
    ) noexcept;

    /// \copydoc IndependentSampler::Impl::get1D
    [[nodiscard]] KIRA_HOST_DEVICE inline float get1D() noexcept;

    /// \copydoc IndependentSampler::Impl::get2D
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec2f get2D() noexcept;

    /// \copydoc IndependentSampler::Impl::getPixel2D
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec2f getPixel2D() noexcept;

private:
    template <typename Function>
    KIRA_HOST_DEVICE decltype(auto) dispatch(Function &&function) noexcept {
        switch (type) {
        case SamplerType::Independent: return std::forward<Function>(function)(storage.independent);
        }
        KIRA_UNREACHABLE();
    }
};

static_assert(std::is_standard_layout_v<IndependentSampler::Impl>);
static_assert(std::is_trivially_copyable_v<IndependentSampler::Impl>);
static_assert(std::is_standard_layout_v<Sampler::Impl>);
static_assert(std::is_trivially_copyable_v<Sampler::Impl>);
static_assert(std::is_trivially_default_constructible_v<Sampler::Impl>);
} // namespace flux
