#pragma once

#include <cuda/std/variant>
#include <type_traits>
#include <utility>

#include "flux/Scene/Geometry.h"
#include "flux/Scene/TriangleMeshImpl.h"

namespace flux {
struct Geometry::Impl : cuda::std::variant<TriangleMesh::Impl> {
    using Base = cuda::std::variant<TriangleMesh::Impl>;
    using Base::Base;

private:
    template <typename Function>
    KIRA_HOST_DEVICE decltype(auto) dispatch(Function &&function) const noexcept {
        return cuda::std::visit(std::forward<Function>(function), static_cast<Base const &>(*this));
    }

public:
    [[nodiscard]] KIRA_HOST_DEVICE GeometryInteraction
    computeInteraction(PreliminaryIntersection const &preliminary) const noexcept {
        return dispatch([&](auto const &geometry) {
            return geometry.computeInteraction(preliminary);
        });
    }

    [[nodiscard]] KIRA_HOST_DEVICE Vec3f interpolateShadingNormal(
        PreliminaryIntersection const &preliminary, Vec3f const &geometricNormal
    ) const noexcept {
        return dispatch([&](auto const &geometry) {
            return geometry.interpolateShadingNormal(preliminary, geometricNormal);
        });
    }

    [[nodiscard]] KIRA_HOST_DEVICE Vec2f
    interpolateTexCoord(PreliminaryIntersection const &preliminary) const noexcept {
        return dispatch([&](auto const &geometry) {
            return geometry.interpolateTexCoord(preliminary);
        });
    }

    [[nodiscard]] KIRA_HOST_DEVICE GeometrySample sample(Vec2f const &value) const noexcept {
        return dispatch([&](auto const &geometry) { return geometry.sample(value); });
    }

    [[nodiscard]] KIRA_HOST_DEVICE float pdf(std::uint32_t elementIndex) const noexcept {
        return dispatch([&](auto const &geometry) { return geometry.pdf(elementIndex); });
    }
};

static_assert(std::is_trivially_copyable_v<Geometry::Impl>);
} // namespace flux
