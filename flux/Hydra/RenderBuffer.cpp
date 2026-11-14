#include "flux/Hydra/RenderBuffer.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/work/loops.h>
#include <pxr/imaging/hd/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <variant>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
template <typename Component>
[[nodiscard]] Component componentOf(flux::Vec3f const &value, std::size_t component) {
    return static_cast<Component>(component < 3 ? value[component] : 1.0F);
}

template <typename Component, typename Scalar>
    requires std::is_arithmetic_v<Scalar>
[[nodiscard]] Component componentOf(Scalar value, std::size_t component) {
    if (component == 0)
        return static_cast<Component>(value);
    return component < 3 ? Component{0} : Component{1};
}

/// \brief Writes \p pixels, whose rows run top row first, to \p data, whose rows run bottom row
/// first.
template <std::size_t Components, typename Value, typename Component>
void writeRows(
    std::span<Value const> pixels, Component *data, std::size_t width, std::size_t height
) {
    pxr::WorkParallelForN(height, [&](std::size_t begin, std::size_t end) {
        for (auto row = begin; row < end; ++row) {
            auto const *const source = pixels.data() + (height - 1 - row) * width;
            auto *const destination = data + row * width * Components;
            for (std::size_t x = 0; x < width; ++x)
                for (std::size_t component = 0; component < Components; ++component)
                    destination[x * Components + component] =
                        componentOf<Component>(source[x], component);
        }
    });
}

/// \brief Calls \p function with the component count of \p format as its template argument.
template <typename Function> void visitComponentCount(pxr::HdFormat format, Function &&function) {
    // The format has one to four components.
    switch (pxr::HdGetComponentCount(format)) {
    case 1: function.template operator()<1>(); return;
    case 2: function.template operator()<2>(); return;
    case 3: function.template operator()<3>(); return;
    default: function.template operator()<4>(); return;
    }
}

template <typename Value>
void writePixels(
    std::span<Value const> pixels,
    std::variant<std::vector<float>, std::vector<std::int32_t>> &data, std::size_t width,
    std::size_t height, pxr::HdFormat format
) {
    if (pixels.size() != width * height)
        throw std::invalid_argument("HdFLuxRenderBuffer::Write: pixels do not match the buffer");

    std::visit([&](auto &storage) {
        visitComponentCount(format, [&]<std::size_t Components>() {
            writeRows<Components>(pixels, storage.data(), width, height);
        });
    }, data);
}

template <typename Element>
void readComponents(void const *data, std::size_t size, std::array<double, 4> &components) {
    components[3] = 1.0;
    for (std::size_t index = 0; index < size; ++index)
        components[index] = static_cast<Element const *>(data)[index];
}

template <std::size_t Components, typename Component>
void clearPixels(std::span<Component> data, std::array<double, 4> const &components) {
    pxr::WorkParallelForN(data.size() / Components, [&](std::size_t begin, std::size_t end) {
        // A local copy, so that the compiler knows that the stores do not change it.
        std::array<Component, 4> values{};
        for (std::size_t component = 0; component < 4; ++component)
            values[component] = static_cast<Component>(components[component]);
        auto *const pixels = data.data();
        for (auto pixel = begin; pixel < end; ++pixel)
            for (std::size_t component = 0; component < Components; ++component)
                pixels[pixel * Components + component] = values[component];
    });
}
} // namespace

HdFLuxRenderBuffer::HdFLuxRenderBuffer(pxr::SdfPath const &id) : pxr::HdRenderBuffer(id) {}

bool HdFLuxRenderBuffer::Allocate(pxr::GfVec3i const &dimensions, pxr::HdFormat format, bool) {
    auto const componentFormat = pxr::HdGetComponentFormat(format);
    if (dimensions[2] != 1 ||
        (componentFormat != pxr::HdFormatFloat32 && componentFormat != pxr::HdFormatInt32)) {
        _Deallocate();
        TF_WARN(
            "HdFLuxRenderBuffer: cannot allocate %dx%dx%d pixels of HdFormat %d", dimensions[0],
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
    auto const count = std::size_t{width} * height * pxr::HdGetComponentCount(format);
    if (componentFormat == pxr::HdFormatInt32)
        pixels_.emplace<std::vector<std::int32_t>>(count);
    else
        pixels_.emplace<std::vector<float>>(count);

    // Set last, so that a throw from the allocation leaves no size without storage.
    width_ = width;
    height_ = height;
    format_ = format;
    return true;
}

void *HdFLuxRenderBuffer::Map() {
    ++mappers_;
    return std::visit([](auto &data) -> void * { return data.data(); }, pixels_);
}

void HdFLuxRenderBuffer::Unmap() { --mappers_; }

bool HdFLuxRenderBuffer::IsMapped() const { return mappers_.load() != 0; }

void HdFLuxRenderBuffer::Clear(pxr::VtValue const &value) {
    std::array<double, 4> components{0.0, 0.0, 0.0, 0.0};

    // A value of a known type is opaque unless it has a fourth component.
    auto const type = pxr::HdGetValueTupleType(value);
    if (type.count == 1) {
        auto const *const data = pxr::HdGetValueData(value);
        switch (type.type) {
        case pxr::HdTypeFloat: readComponents<float>(data, 1, components); break;
        case pxr::HdTypeFloatVec2: readComponents<float>(data, 2, components); break;
        case pxr::HdTypeFloatVec3: readComponents<float>(data, 3, components); break;
        case pxr::HdTypeFloatVec4: readComponents<float>(data, 4, components); break;
        case pxr::HdTypeDouble: readComponents<double>(data, 1, components); break;
        case pxr::HdTypeDoubleVec2: readComponents<double>(data, 2, components); break;
        case pxr::HdTypeDoubleVec3: readComponents<double>(data, 3, components); break;
        case pxr::HdTypeDoubleVec4: readComponents<double>(data, 4, components); break;
        case pxr::HdTypeInt32: readComponents<std::int32_t>(data, 1, components); break;
        case pxr::HdTypeInt32Vec2: readComponents<std::int32_t>(data, 2, components); break;
        case pxr::HdTypeInt32Vec3: readComponents<std::int32_t>(data, 3, components); break;
        case pxr::HdTypeInt32Vec4: readComponents<std::int32_t>(data, 4, components); break;
        default: break;
        }
    }

    std::visit([&](auto &data) {
        visitComponentCount(format_, [&]<std::size_t Components>() {
            clearPixels<Components>(std::span{data}, components);
        });
    }, pixels_);
}

void HdFLuxRenderBuffer::Write(std::span<flux::Vec3f const> pixels) {
    writePixels(pixels, pixels_, width_, height_, format_);
}

void HdFLuxRenderBuffer::Write(std::span<float const> pixels) {
    writePixels(pixels, pixels_, width_, height_, format_);
}

void HdFLuxRenderBuffer::Write(std::span<std::int32_t const> pixels) {
    writePixels(pixels, pixels_, width_, height_, format_);
}

void HdFLuxRenderBuffer::_Deallocate() {
    TF_VERIFY(!IsMapped());
    width_ = 0;
    height_ = 0;
    format_ = pxr::HdFormatInvalid;
    std::visit([](auto &data) { data.clear(); }, pixels_);
    converged_.store(false);
}
