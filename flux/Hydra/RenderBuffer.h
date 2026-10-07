#pragma once

#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hd/renderBuffer.h>

#include <atomic>
#include <vector>

/// \brief A render buffer in host memory.
///
/// Holds formats with 32-bit float components. It never multisamples, since the renderer resolves
/// its samples before it writes a buffer.
class HdFluxRenderBuffer final : public pxr::HdRenderBuffer {
public:
    explicit HdFluxRenderBuffer(pxr::SdfPath const &id);

    /// \brief Allocates \p dimensions pixels of \p format.
    /// \return Whether the depth of \p dimensions is one and \p format has 32-bit float
    ///         components.
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

    /// \brief Sets every pixel to the leading components of \p value.
    void Clear(pxr::GfVec4f const &value);

private:
    void _Deallocate() override;

    unsigned int width_ = 0;
    unsigned int height_ = 0;
    pxr::HdFormat format_ = pxr::HdFormatInvalid;

    /// Pixels row by row, bottom row first, each holding the components of \c format_.
    std::vector<float> data_;
    std::atomic<int> mappers_ = 0;
    std::atomic<bool> converged_ = false;
};
