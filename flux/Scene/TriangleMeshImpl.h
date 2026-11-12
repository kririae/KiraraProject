#pragma once

#include <array>
#include <cmath>
#include <limits>

#include "flux/Core/MathUtils.h"
#include "flux/Scene/TriangleMesh.h"

namespace flux {
KIRA_HOST_DEVICE inline float
TriangleMesh::Impl::getTriangleArea(std::uint32_t triangle) const noexcept {
    auto const indices = triangles[triangle];
    auto const vertex0 = vertices[indices[0]];
    auto const edge1 = vertices[indices[1]] - vertex0;
    auto const edge2 = vertices[indices[2]] - vertex0;
    return cross(edge1, edge2).norm() * 0.5F;
}

KIRA_HOST_DEVICE inline Vec3f TriangleMesh::Impl::interpolateShadingNormal(
    PreliminaryIntersection const &preliminary, Vec3f const &geometricNormal
) const noexcept {
    if (!normals)
        return geometricNormal;

    auto const indices = normalIndices[preliminary.elementIndex];
    return interpolateShadingNormal(
        {normals[indices[0]], normals[indices[1]], normals[indices[2]]}, preliminary.coordinates,
        geometricNormal
    );
}

KIRA_HOST_DEVICE inline Vec3f TriangleMesh::Impl::interpolateShadingNormal(
    std::array<Vec3f, 3> const &cornerNormals, Vec2f const &coordinates,
    Vec3f const &geometricNormal
) noexcept {
    auto const u = coordinates.x();
    auto const v = coordinates.y();
    auto const w = 1.0F - u - v;
    auto const interpolated = cornerNormals[0] * w + cornerNormals[1] * u + cornerNormals[2] * v;
    auto const lengthSquared = interpolated.norm2();
    if (lengthSquared > 1.0e-20F && lengthSquared < std::numeric_limits<float>::max())
        return interpolated.normalize();
    return geometricNormal;
}

KIRA_HOST_DEVICE inline GeometryInteraction
TriangleMesh::Impl::computeInteraction(PreliminaryIntersection const &preliminary) const noexcept {
    // Every load comes before the arithmetic, and a select, not a branch, skips an absent array.
    // The three index arrays load together, and then the three attribute arrays.
    auto const element = preliminary.elementIndex;
    auto const indices = triangles[element];
    auto const textureIndices = texCoords ? texCoordIndices[element] : Vec3u{};
    auto const shadingIndices = normals ? normalIndices[element] : Vec3u{};
    auto const vertex0 = vertices[indices[0]];
    auto const vertex1 = vertices[indices[1]];
    auto const vertex2 = vertices[indices[2]];
    auto const texture0 = texCoords ? texCoords[textureIndices[0]] : Vec2f{};
    auto const texture1 = texCoords ? texCoords[textureIndices[1]] : Vec2f{};
    auto const texture2 = texCoords ? texCoords[textureIndices[2]] : Vec2f{};
    auto const shadingNormal0 = normals ? normals[shadingIndices[0]] : Vec3f{};
    auto const shadingNormal1 = normals ? normals[shadingIndices[1]] : Vec3f{};
    auto const shadingNormal2 = normals ? normals[shadingIndices[2]] : Vec3f{};

    auto const u = preliminary.coordinates.x();
    auto const v = preliminary.coordinates.y();
    auto const w = 1.0F - u - v;
    auto const edge1 = vertex1 - vertex0;
    auto const edge2 = vertex2 - vertex0;
    auto const areaNormal = cross(edge1, edge2);
    auto const normal = areaNormal.normalize();

    // The parameterization gradients reuse the barycentric gradients of b and c, which need
    // the same triangle area. Differentiating the edge vectors directly keeps a small offset
    // from rounding away against a large vertex position.
    auto uv = Vec2f{};
    auto uvGradU = Vec3f{};
    auto uvGradV = Vec3f{};
    if (texCoords) {
        auto const inverseArea2 = 1.0F / areaNormal.norm2();
        auto const gradientB = cross(edge2, areaNormal) * inverseArea2;
        auto const gradientC = cross(areaNormal, edge1) * inverseArea2;
        auto const deltaUvB = texture1 - texture0;
        auto const deltaUvC = texture2 - texture0;
        uv = texture0 * w + texture1 * u + texture2 * v;
        uvGradU = gradientB * deltaUvB.x() + gradientC * deltaUvC.x();
        uvGradV = gradientB * deltaUvB.y() + gradientC * deltaUvC.y();
    }

    return {
        .position = vertex0 + edge1 * u + edge2 * v,
        .geometricNormal = normal,
        .shadingNormal = normals ? interpolateShadingNormal(
                                       {shadingNormal0, shadingNormal1, shadingNormal2},
                                       preliminary.coordinates, normal
                                   )
                                 : normal,
        .uv = uv,
        .uvGradU = uvGradU,
        .uvGradV = uvGradV,
        .elementIndex = preliminary.elementIndex,
    };
}

KIRA_HOST_DEVICE inline GeometrySample
TriangleMesh::Impl::sample(Vec2f const &sampleValue) const noexcept {
    if (!triangleAreaBins || !triangleAreaPDF || !(surfaceArea > 0.0F) || numTriangles == 0)
        return {};

    float remapped;
    auto const triangle =
        sampleAliasTable(triangleAreaBins, numTriangles, sampleValue.x(), remapped);

#if 0 // Enable with a low-discrepancy sampler.
    auto const barycentric = lowDiscrepancySampleTriangle(sampleValue.y());
#else
    auto const root = std::sqrt(remapped);
    auto const b1 = sampleValue.y() * root;
    auto const b2 = root - b1;
    auto const barycentric = Vec2f{b1, b2};
#endif
    return {
        .coordinates = barycentric,
        .elementIndex = triangle,
        .pdf = triangleAreaPDF[triangle],
    };
}

KIRA_HOST_DEVICE inline float TriangleMesh::Impl::pdf(std::uint32_t triangle) const noexcept {
    if (!triangleAreaPDF || triangle >= numTriangles)
        return 0.0F;
    return triangleAreaPDF[triangle];
}
} // namespace flux
