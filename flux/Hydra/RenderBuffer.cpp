#include "flux/Hydra/RenderBuffer.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/types.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

// The Tf diagnostic macros name Tf without its namespace.
PXR_NAMESPACE_USING_DIRECTIVE

HdFluxRenderBuffer::HdFluxRenderBuffer(pxr::SdfPath const &id) : pxr::HdRenderBuffer(id) {}

bool HdFluxRenderBuffer::Allocate(pxr::GfVec3i const &dimensions, pxr::HdFormat format, bool) {
    if (dimensions[2] != 1 || pxr::HdGetComponentFormat(format) != pxr::HdFormatFloat32) {
        _Deallocate();
        TF_WARN(
            "HdFluxRenderBuffer: cannot allocate %dx%dx%d pixels of HdFormat %d", dimensions[0],
            dimensions[1], dimensions[2], static_cast<int>(format)
        );
        return false;
    }

    // A buffer that stays as it is keeps its pixels.
    auto const width = static_cast<unsigned int>(dimensions[0]);
    auto const height = static_cast<unsigned int>(dimensions[1]);
    if (width == width_ && height == height_ && format == format_)
        return true;

    _Deallocate();
    data_.resize(std::size_t{width} * height * pxr::HdGetComponentCount(format));

    // Set last, so that a throw from the allocation leaves no size without storage.
    width_ = width;
    height_ = height;
    format_ = format;
    return true;
}

void *HdFluxRenderBuffer::Map() {
    ++mappers_;
    return data_.data();
}

void HdFluxRenderBuffer::Unmap() { --mappers_; }

bool HdFluxRenderBuffer::IsMapped() const { return mappers_.load() != 0; }

void HdFluxRenderBuffer::Clear(pxr::VtValue const &value) {
    std::array<float, 4> components{0.0F, 0.0F, 0.0F, 0.0F};

    // A value of a known type is opaque unless it has a fourth component.
    auto const type = pxr::HdGetValueTupleType(value);
    if (type.count == 1) {
        auto const *const data = pxr::HdGetValueData(value);
        std::size_t size{0};
        bool isDouble{false};

        switch (type.type) {
        case pxr::HdTypeFloat: size = 1; break;
        case pxr::HdTypeFloatVec2: size = 2; break;
        case pxr::HdTypeFloatVec3: size = 3; break;
        case pxr::HdTypeFloatVec4: size = 4; break;
        case pxr::HdTypeDouble: size = 1, isDouble = true; break;
        case pxr::HdTypeDoubleVec2: size = 2, isDouble = true; break;
        case pxr::HdTypeDoubleVec3: size = 3, isDouble = true; break;
        case pxr::HdTypeDoubleVec4: size = 4, isDouble = true; break;
        default: break;
        }

        if (size != 0)
            components[3] = 1.0F;
        for (std::size_t index = 0; index < size; ++index)
            components[index] = isDouble
                                    ? static_cast<float>(static_cast<double const *>(data)[index])
                                    : static_cast<float const *>(data)[index];
    }

    auto const count = pxr::HdGetComponentCount(format_);
    for (auto pixel = data_.begin(); pixel != data_.end(); pixel += count)
        std::copy_n(components.begin(), count, pixel);
}

void HdFluxRenderBuffer::Assign(std::span<flux::Vec3f const> pixels) {
    if (pixels.size() != std::size_t{width_} * height_)
        throw std::invalid_argument("HdFluxRenderBuffer::Assign: pixels do not match the buffer");

    auto const components = pxr::HdGetComponentCount(format_);

    for (std::size_t row = 0; row < height_; ++row) {
        // The buffer holds the bottom row first.
        auto const source = pixels.subspan((height_ - 1 - row) * width_, width_);
        auto destination = data_.begin() + static_cast<std::ptrdiff_t>(row * width_ * components);
        for (auto const &value : source)
            for (std::size_t component = 0; component < components; ++component)
                *destination++ = component < 3 ? value[component] : 1.0F;
    }
}

void HdFluxRenderBuffer::_Deallocate() {
    TF_VERIFY(!IsMapped());
    width_ = 0;
    height_ = 0;
    format_ = pxr::HdFormatInvalid;
    data_.clear();
    converged_.store(false);
}
