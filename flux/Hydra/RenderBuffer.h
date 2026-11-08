#pragma once

#include <pxr/base/gf/vec3i.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/renderBuffer.h>

#include <atomic>
#include <span>
#include <vector>

#include "flux/Core/Math.h"

/// \brief A render buffer in host memory.
class HdFluxRenderBuffer final : public pxr::HdRenderBuffer {
public:
    explicit HdFluxRenderBuffer(pxr::SdfPath const &id);

    /// \brief Allocates \p dimensions pixels of \p format.
    ///
    /// A call that repeats the dimensions and the format of the buffer keeps its pixels and its
    /// converged state. Any other call discards the pixels and clears the state.
    ///
    /// \return Whether the depth of \p dimensions is one and \p format has 32-bit float
    ///         components.
    /// \throw std::bad_alloc If the memory is not available. The buffer then holds no pixels.
    bool Allocate(pxr::GfVec3i const &dimensions, pxr::HdFormat format, bool multiSampled) override;

    unsigned int GetWidth() const override { return width_; }
    unsigned int GetHeight() const override { return height_; }
    unsigned int GetDepth() const override { return 1; }
    pxr::HdFormat GetFormat() const override { return format_; }
    bool IsMultiSampled() const override { return false; }

    void *Map() override;
    void Unmap() override;
    bool IsMapped() const override;

    bool IsConverged() const override { return converged_.load(); }
    void SetConverged(bool converged) noexcept { converged_.store(converged); }

    void Resolve() override {}

    /// \brief Sets every pixel to \p value, converted to the buffer's format.
    void Clear(pxr::VtValue const &value);

    /// \brief Sets the pixels to \p pixels, which hold the rows of an image, top row first.
    ///
    /// A pixel takes the leading components of its value. A further component is 0, and a fourth
    /// component is 1.
    ///
    /// \pre \p pixels has one value per pixel of the buffer.
    void Write(std::span<flux::Vec3f const> pixels);
    void Write(std::span<float const> pixels);

private:
    void _Deallocate() override;

    unsigned int width_ = 0;
    unsigned int height_ = 0;
    pxr::HdFormat format_ = pxr::HdFormatInvalid;

    /// Pixels row by row, bottom row first, each holding the components of \c format_.
    std::vector<float> pixels_;
    std::atomic<int> mappers_ = 0;
    std::atomic<bool> converged_ = false;
};
