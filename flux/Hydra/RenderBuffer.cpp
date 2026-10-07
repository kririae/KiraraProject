#include "flux/Hydra/RenderBuffer.h"

#include <pxr/base/tf/diagnostic.h>

#include <algorithm>
#include <cstddef>

// The Tf diagnostic macros name Tf without its namespace.
PXR_NAMESPACE_USING_DIRECTIVE

HdFluxRenderBuffer::HdFluxRenderBuffer(pxr::SdfPath const &id) : pxr::HdRenderBuffer(id) {}

bool HdFluxRenderBuffer::Allocate(pxr::GfVec3i const &dimensions, pxr::HdFormat format, bool) {
    _Deallocate();
    if (dimensions[2] != 1 || pxr::HdGetComponentFormat(format) != pxr::HdFormatFloat32) {
        TF_WARN(
            "HdFluxRenderBuffer: cannot allocate %dx%dx%d pixels of HdFormat %d", dimensions[0],
            dimensions[1], dimensions[2], static_cast<int>(format)
        );
        return false;
    }
    width_ = static_cast<unsigned int>(dimensions[0]);
    height_ = static_cast<unsigned int>(dimensions[1]);
    format_ = format;
    data_.resize(std::size_t{width_} * height_ * pxr::HdGetComponentCount(format));
    return true;
}

void *HdFluxRenderBuffer::Map() {
    ++mappers_;
    return data_.data();
}

void HdFluxRenderBuffer::Unmap() { --mappers_; }

bool HdFluxRenderBuffer::IsMapped() const { return mappers_.load() != 0; }

void HdFluxRenderBuffer::Clear(pxr::GfVec4f const &value) {
    auto const components = pxr::HdGetComponentCount(format_);
    for (auto pixel = data_.begin(); pixel != data_.end(); pixel += components)
        std::copy_n(value.data(), components, pixel);
}

void HdFluxRenderBuffer::_Deallocate() {
    TF_VERIFY(!IsMapped());
    width_ = 0;
    height_ = 0;
    format_ = pxr::HdFormatInvalid;
    data_.clear();
    converged_.store(false);
}
