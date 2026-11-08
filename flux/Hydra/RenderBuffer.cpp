#include "flux/Hydra/RenderBuffer.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/work/loops.h>
#include <pxr/imaging/hd/types.h>

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
[[nodiscard]] float componentOf(flux::Vec3f const &value, std::size_t component) {
    return component < 3 ? value[component] : 1.0F;
}

/// \brief Writes \p pixels, whose rows run top row first, to \p data, whose rows run bottom row
/// first.
template <std::size_t Components, typename Value>
void assignRows(std::span<Value const> pixels, float *data, std::size_t width, std::size_t height) {
    pxr::WorkParallelForN(height, [&](std::size_t begin, std::size_t end) {
        for (auto row = begin; row < end; ++row) {
            auto const *const source = pixels.data() + (height - 1 - row) * width;
            auto *const destination = data + row * width * Components;
            for (std::size_t x = 0; x < width; ++x)
                for (std::size_t component = 0; component < Components; ++component)
                    destination[x * Components + component] = componentOf(source[x], component);
        }
    });
}

/// \brief Calls \p function with the component count of \p format as its template argument.
template <typename Function> void withComponentCount(pxr::HdFormat format, Function &&function) {
    // The format has one to four components.
    switch (pxr::HdGetComponentCount(format)) {
    case 1: function.template operator()<1>(); return;
    case 2: function.template operator()<2>(); return;
    case 3: function.template operator()<3>(); return;
    default: function.template operator()<4>(); return;
    }
}

template <typename Value>
void assignPixels(
    std::span<Value const> pixels, std::span<float> data, std::size_t width, std::size_t height,
    pxr::HdFormat format
) {
    if (pixels.size() != width * height)
        throw std::invalid_argument("HdFluxRenderBuffer::Assign: pixels do not match the buffer");

    withComponentCount(format, [&]<std::size_t Components>() {
        assignRows<Components>(pixels, data.data(), width, height);
    });
}

/// \brief Sets every pixel of \p data, which holds \c Components floats per pixel, to the leading
/// \c Components of \p components.
template <std::size_t Components>
void fillPixels(std::span<float> data, std::array<float, 4> const &components) {
    pxr::WorkParallelForN(data.size() / Components, [&](std::size_t begin, std::size_t end) {
        // A local copy, so that the compiler knows that the stores do not change it.
        auto const values = components;
        auto *const pixels = data.data();
        for (auto pixel = begin; pixel < end; ++pixel)
            for (std::size_t component = 0; component < Components; ++component)
                pixels[pixel * Components + component] = values[component];
    });
}
} // namespace

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

    withComponentCount(format_, [&]<std::size_t Components>() {
        fillPixels<Components>(std::span{data_}, components);
    });
}

void HdFluxRenderBuffer::Assign(std::span<flux::Vec3f const> pixels) {
    assignPixels(pixels, std::span{data_}, width_, height_, format_);
}

void HdFluxRenderBuffer::_Deallocate() {
    TF_VERIFY(!IsMapped());
    width_ = 0;
    height_ = 0;
    format_ = pxr::HdFormatInvalid;
    data_.clear();
    converged_.store(false);
}
