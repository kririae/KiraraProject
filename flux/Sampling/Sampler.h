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
    /// Owen-scrambled Sobol sampling.
    Sobol,
};

/// \brief What a sample drawn at one path vertex is used for.
///
/// A use and a vertex depth together name one set of sample dimensions. Draws with different
/// names take independent sets, so a draw keeps its dimensions whatever else a path draws.
enum class SampleUse : std::uint8_t {
    /// Position within the pixel, at the camera vertex.
    Pixel,
    /// Position on the lens, at the camera vertex.
    Lens,
    /// Light selection and position on the light.
    Light,
    /// BSDF lobe selection and direction.
    Bsdf,
    /// Russian roulette.
    Terminate,
};

/// Number of \c SampleUse values.
inline constexpr std::uint32_t sampleUseCount = 5;
static_assert(static_cast<std::uint32_t>(SampleUse::Terminate) + 1 == sampleUseCount);

/// \brief Host-side sampling configuration selected by a context.
///
/// The first sampler successfully added to a context becomes active.
class Sampler : public RenderObject {
    friend class TXContext;

protected:
    Sampler(TXContext &tx, SamplerType type);

public:
    struct Impl;

    [[nodiscard]] SamplerType getType() const noexcept { return type_; }
    [[nodiscard]] bool isRoot() const noexcept override { return true; }
    [[nodiscard]] Impl getImpl() const;

private:
    [[nodiscard]] static Ref<Sampler> create(TXContext &tx, kira::Properties const &props);

    SamplerType type_;
};

/// \brief Independent pseudorandom sampler.
class IndependentSampler final : public Sampler {
    friend class TXContext;

public:
    struct Impl;

    /// \brief Creates the concrete sampler.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    IndependentSampler(TXContext &tx, kira::Properties const &props);
};

/// \brief Independent pseudorandom sampling implementation.
///
/// Draws advance one stream in call order, so the use and depth of a draw do not affect it.
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
    ///
    /// \param use What the sample is for.
    /// \param depth Depth of the path vertex drawing it.
    [[nodiscard]] KIRA_HOST_DEVICE inline float get1D(SampleUse use, std::uint32_t depth) noexcept;

    /// \brief Returns a uniformly distributed point in \f$[0,1)^2\f$.
    /// \copydetails get1D
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec2f get2D(SampleUse use, std::uint32_t depth) noexcept;

    /// \brief Returns a uniformly distributed point in \f$[0,1)^3\f$.
    /// \copydetails get1D
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec3f get3D(SampleUse use, std::uint32_t depth) noexcept;
};

/// \brief Owen-scrambled Sobol sampler.
///
/// Each dimension set is a shuffled, scrambled 1D, 2D, or 3D Sobol sequence, stratified within
/// itself and decorrelated from every other set. This is the padded construction of Burley,
/// "Practical Hash-based Owen Scrambling", JCGT 9(4), 2020.
///
/// A pixel takes at most \c maxSamplesPerPixel samples. Later samples repeat the points of
/// earlier ones, so the estimate stops converging but stays unbiased.
class SobolSampler final : public Sampler {
    friend class TXContext;

public:
    struct Impl;

    /// \copydoc IndependentSampler::getImpl
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    SobolSampler(TXContext &tx, kira::Properties const &props);
};

/// \brief Owen-scrambled Sobol sampling implementation.
struct SobolSampler::Impl {
    /// Samples a pixel can take before its sequences repeat.
    static constexpr std::uint32_t maxSamplesPerPixel = 1U << 16U;

    /// Sample index within the pixel.
    std::uint32_t index;

    /// Per-pixel scrambling seed.
    std::uint32_t seed;

public:
    /// \copydoc IndependentSampler::Impl::startPixelSample
    KIRA_HOST_DEVICE inline void startPixelSample(
        Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
    ) noexcept;

    /// \copydoc IndependentSampler::Impl::get1D
    [[nodiscard]] KIRA_HOST_DEVICE inline float get1D(SampleUse use, std::uint32_t depth) noexcept;

    /// \copydoc IndependentSampler::Impl::get2D
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec2f get2D(SampleUse use, std::uint32_t depth) noexcept;

    /// \copydoc IndependentSampler::Impl::get3D
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec3f get3D(SampleUse use, std::uint32_t depth) noexcept;
};
///
/// The explicit dispatch keeps the sampler type visible to OptiX bound-value
/// specialization.
struct Sampler::Impl {
    /// Concrete implementation selected for this dispatcher.
    SamplerType type;

    /// Concrete sampler storage selected by \c type.
    union Storage {
        /// Independent sampler state.
        IndependentSampler::Impl independent;
        /// Sobol sampler state.
        SobolSampler::Impl sobol;
    } storage;

public:
    /// \copydoc IndependentSampler::Impl::startPixelSample
    KIRA_HOST_DEVICE inline void startPixelSample(
        Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
    ) noexcept;

    /// \copydoc IndependentSampler::Impl::get1D
    [[nodiscard]] KIRA_HOST_DEVICE inline float get1D(SampleUse use, std::uint32_t depth) noexcept;

    /// \copydoc IndependentSampler::Impl::get2D
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec2f get2D(SampleUse use, std::uint32_t depth) noexcept;

    /// \copydoc IndependentSampler::Impl::get3D
    [[nodiscard]] KIRA_HOST_DEVICE inline Vec3f get3D(SampleUse use, std::uint32_t depth) noexcept;

private:
    template <typename Function>
    KIRA_HOST_DEVICE decltype(auto) dispatch(Function &&function) noexcept {
        switch (type) {
        case SamplerType::Independent: return std::forward<Function>(function)(storage.independent);
        case SamplerType::Sobol: return std::forward<Function>(function)(storage.sobol);
        }
        KIRA_UNREACHABLE();
    }
};

static_assert(std::is_standard_layout_v<IndependentSampler::Impl>);
static_assert(std::is_trivially_copyable_v<IndependentSampler::Impl>);
static_assert(std::is_standard_layout_v<SobolSampler::Impl>);
static_assert(std::is_trivially_copyable_v<SobolSampler::Impl>);
static_assert(sizeof(SobolSampler::Impl) <= sizeof(IndependentSampler::Impl));
static_assert(std::is_standard_layout_v<Sampler::Impl>);
static_assert(std::is_trivially_copyable_v<Sampler::Impl>);
static_assert(std::is_trivially_default_constructible_v<Sampler::Impl>);
} // namespace flux
