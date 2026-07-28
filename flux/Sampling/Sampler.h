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
class Sampler : public RenderObject {
protected:
    /// \brief Constructs a sampler in \p tx.
    Sampler(TXContext &tx, kira::Properties properties, SamplerType type);

    /// \copydoc ContextObject::registerTo
    void registerTo(TXContext &tx) override;

public:
    /// \brief Device-side sampler dispatcher.
    struct DeviceImpl;

    /// \brief Returns the concrete sampler type.
    [[nodiscard]] SamplerType getType() const noexcept { return type_; }

    /// \brief Creates the device-side sampler dispatcher for \p resolution.
    ///
    /// \param resolution Nonzero image resolution for the launch.
    [[nodiscard]] DeviceImpl getDeviceImpl(Vec2u const &resolution) const;

private:
    SamplerType type_;
};

/// \brief Independent pseudorandom sampler.
class IndependentSampler final : public Sampler {
    friend class TXContext;

public:
    /// \brief Device-side independent sampler.
    struct DeviceImpl;

    /// \brief Creates the concrete device-side sampler for \p resolution.
    ///
    /// \param resolution Nonzero image resolution for the launch.
    [[nodiscard]] DeviceImpl getDeviceImpl(Vec2u const &resolution) const noexcept;

private:
    IndependentSampler(TXContext &tx, kira::Properties properties);
};

/// \brief Device-side independent sampler.
struct IndependentSampler::DeviceImpl {
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
    KIRA_DEVICE inline void startPixelSample(
        Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
    ) noexcept;

    /// \brief Returns a uniformly distributed value in \f$[0,1)\f$.
    [[nodiscard]] KIRA_DEVICE inline float get1D() noexcept;

    /// \brief Returns two uniformly distributed values in \f$[0,1)^2\f$.
    [[nodiscard]] KIRA_DEVICE inline Vec2f get2D() noexcept;

    /// \brief Returns a sample within the current pixel.
    ///
    /// The result lies in \f$[0,1)^2\f$.
    [[nodiscard]] KIRA_DEVICE inline Vec2f getPixel2D() noexcept;
};

/// \brief Device-side sampler dispatcher.
///
/// The explicit dispatch switch keeps the discriminator visible to OptiX bound-value
/// specialization.
struct Sampler::DeviceImpl {
    /// Concrete implementation selected for this dispatcher.
    SamplerType type;

    /// Concrete sampler storage selected by \c type.
    union Storage {
        /// Independent sampler state.
        IndependentSampler::DeviceImpl independent;
    } storage;

public:
    /// \copydoc IndependentSampler::DeviceImpl::startPixelSample
    KIRA_DEVICE inline void startPixelSample(
        Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
    ) noexcept;

    /// \copydoc IndependentSampler::DeviceImpl::get1D
    [[nodiscard]] KIRA_DEVICE inline float get1D() noexcept;

    /// \copydoc IndependentSampler::DeviceImpl::get2D
    [[nodiscard]] KIRA_DEVICE inline Vec2f get2D() noexcept;

    /// \copydoc IndependentSampler::DeviceImpl::getPixel2D
    [[nodiscard]] KIRA_DEVICE inline Vec2f getPixel2D() noexcept;

private:
    template <typename Function> KIRA_DEVICE decltype(auto) dispatch(Function &&function) noexcept {
        switch (type) {
        case SamplerType::Independent: return std::forward<Function>(function)(storage.independent);
        }
        KIRA_UNREACHABLE();
    }
};

static_assert(std::is_standard_layout_v<IndependentSampler::DeviceImpl>);
static_assert(std::is_trivially_copyable_v<IndependentSampler::DeviceImpl>);
static_assert(std::is_standard_layout_v<Sampler::DeviceImpl>);
static_assert(std::is_trivially_copyable_v<Sampler::DeviceImpl>);
static_assert(std::is_trivially_default_constructible_v<Sampler::DeviceImpl>);
} // namespace flux
