#include "flux/Scene/TriangleMesh.h"

#include <tbb/parallel_for.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <rapidobj/rapidobj.hpp>
#include <type_traits>
#include <utility>

#include "flux/Core/KIRA.h"
#include "flux/Core/MathUtils.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
template <typename Array> void release(Array &array) { array = std::remove_cvref_t<Array>{}; }

void generateVertexNormals(
    std::span<Vec3f const> vertices, std::span<Vec3u const> triangles,
    kira::SmallVector<Vec3f, 0> &normals
) {
    normals.resize_for_overwrite(vertices.size());
    tbb::parallel_for(std::size_t{0}, normals.size(), [&](std::size_t index) {
        normals[index] = Vec3f{0.0F};
    });

    // Use libigl's angle-weighted vertex-normal formula. Workers add each
    // contribution to the shared normal array with relaxed atomics.
    tbb::parallel_for(std::size_t{0}, triangles.size(), [&](std::size_t triangleIndex) {
        auto const &triangle = triangles[triangleIndex];
        auto const &vertex0 = vertices[triangle[0]];
        auto const &vertex1 = vertices[triangle[1]];
        auto const &vertex2 = vertices[triangle[2]];
        auto const faceVector = cross(vertex1 - vertex0, vertex2 - vertex0);
        auto const faceLengthSquared = faceVector.norm2();
        if (!(faceLengthSquared > 0.0F) || !std::isfinite(faceLengthSquared))
            return;
        auto const faceNormal = faceVector / std::sqrt(faceLengthSquared);

        for (std::size_t corner = 0; corner < 3; ++corner) {
            auto const &center = vertices[triangle[corner]];
            auto previous = vertices[triangle[(corner + 2) % 3]] - center;
            auto next = vertices[triangle[(corner + 1) % 3]] - center;
            auto const previousLengthSquared = previous.norm2();
            auto const nextLengthSquared = next.norm2();
            if (!(previousLengthSquared > 0.0F) || !std::isfinite(previousLengthSquared) ||
                !(nextLengthSquared > 0.0F) || !std::isfinite(nextLengthSquared))
                continue;

            previous = previous / std::sqrt(previousLengthSquared);
            next = next / std::sqrt(nextLengthSquared);
            auto const angle =
                2.0F * std::atan2((previous - next).norm(), (previous + next).norm());
            auto const contribution = faceNormal * angle;
            auto &normal = normals[triangle[corner]];
            for (std::size_t component = 0; component < 3; ++component)
                std::atomic_ref<float>(normal[component])
                    .fetch_add(contribution[component], std::memory_order_relaxed);
        }
    });

    tbb::parallel_for(std::size_t{0}, normals.size(), [&](std::size_t index) {
        auto const lengthSquared = normals[index].norm2();
        if (lengthSquared > 0.0F && std::isfinite(lengthSquared))
            normals[index] = normals[index] / std::sqrt(lengthSquared);
    });
}
} // namespace

TriangleMesh::TriangleMesh(TXContext &tx, kira::Properties properties)
    : Geometry(tx, std::move(properties), GeometryType::TriangleMesh) {
    loadObj(getProperties().use<std::filesystem::path>("path"));
}

TriangleMesh::Impl TriangleMesh::getImpl() const noexcept {
    auto const vertices = getVertices();
    return {
        .vertices = vertices.data(),
        .triangles = triangles_.data(),
        .normals = normals_.empty() ? nullptr : normals_.data(),
        .normalIndices = normals_.empty()
                             ? nullptr
                             : (normalIndices_.empty() ? triangles_.data() : normalIndices_.data()),
        .texCoords = texCoords_.empty() ? nullptr : texCoords_.data(),
        .texCoordIndices =
            texCoords_.empty()
                ? nullptr
                : (texCoordIndices_.empty() ? triangles_.data() : texCoordIndices_.data()),
        .numVertices = static_cast<std::uint32_t>(vertices.size()),
        .numTriangles = static_cast<std::uint32_t>(triangles_.size()),
    };
}

void TriangleMesh::loadObj(std::filesystem::path const &path) {
    auto result = rapidobj::ParseFile(path, rapidobj::MaterialLibrary::Ignore());
    if (result.error)
        throw kira::Anyhow(
            "TriangleMesh: failed to load OBJ '{}': {}", path.string(), result.error.code.message()
        );
    if (!rapidobj::Triangulate(result))
        throw kira::Anyhow(
            "TriangleMesh: failed to triangulate OBJ '{}': {}", path.string(),
            result.error.code.message()
        );

    auto const &attributes = result.attributes;
    if (attributes.positions.empty() || attributes.positions.size() % 3 != 0)
        throw kira::Anyhow("TriangleMesh: OBJ '{}' contains no valid vertices", path.string());
    if (attributes.normals.size() % 3 != 0)
        throw kira::Anyhow("TriangleMesh: OBJ '{}' contains malformed normals", path.string());
    if (attributes.texcoords.size() % 2 != 0)
        throw kira::Anyhow(
            "TriangleMesh: OBJ '{}' contains malformed texture coordinates", path.string()
        );

    auto const numVertices = attributes.positions.size() / 3;
    auto const numNormals = attributes.normals.size() / 3;
    auto const numTexCoords = attributes.texcoords.size() / 2;
    if (numVertices > std::numeric_limits<std::uint32_t>::max())
        throw kira::Anyhow("TriangleMesh: OBJ '{}' has too many vertices", path.string());

    kira::SmallVector<std::size_t, 0> triangleOffsets;
    triangleOffsets.resize_for_overwrite(result.shapes.size() + 1);
    triangleOffsets[0] = 0;

    bool anyNormalIndex = false;
    bool allCornersHaveNormals = true;
    bool normalIndicesAliasVertices = true;
    bool anyTexCoordIndex = false;
    bool allCornersHaveTexCoords = true;
    bool texCoordIndicesAliasVertices = true;

    // Validate all parsed OBJ data before writing TriangleMesh arrays.
    for (std::size_t shapeIndex = 0; shapeIndex < result.shapes.size(); ++shapeIndex) {
        auto const &mesh = result.shapes[shapeIndex].mesh;
        auto const numTriangles = mesh.num_face_vertices.size();
        if (numTriangles > std::numeric_limits<std::size_t>::max() / 3 ||
            mesh.indices.size() != numTriangles * 3)
            throw kira::Anyhow(
                "TriangleMesh: OBJ '{}' contains an invalid triangulated mesh", path.string()
            );
        for (auto const numFaceVertices : mesh.num_face_vertices)
            if (numFaceVertices != 3)
                throw kira::Anyhow(
                    "TriangleMesh: OBJ '{}' contains an invalid triangulated face", path.string()
                );

        auto const previousCount = triangleOffsets[shapeIndex];
        if (numTriangles > std::numeric_limits<std::uint32_t>::max() - previousCount)
            throw kira::Anyhow("TriangleMesh: OBJ '{}' has too many triangles", path.string());
        triangleOffsets[shapeIndex + 1] = previousCount + numTriangles;

        for (auto const &index : mesh.indices) {
            if (index.position_index < 0 ||
                std::cmp_greater_equal(index.position_index, numVertices))
                throw kira::Anyhow(
                    "TriangleMesh: OBJ '{}' contains an invalid vertex index", path.string()
                );

            if (index.normal_index < 0) {
                allCornersHaveNormals = false;
                normalIndicesAliasVertices = false;
            } else {
                anyNormalIndex = true;
                if (std::cmp_greater_equal(index.normal_index, numNormals))
                    throw kira::Anyhow(
                        "TriangleMesh: OBJ '{}' contains an invalid normal index", path.string()
                    );
                normalIndicesAliasVertices &= index.normal_index == index.position_index;
            }

            if (index.texcoord_index < 0) {
                allCornersHaveTexCoords = false;
                texCoordIndicesAliasVertices = false;
            } else {
                anyTexCoordIndex = true;
                if (std::cmp_greater_equal(index.texcoord_index, numTexCoords))
                    throw kira::Anyhow(
                        "TriangleMesh: OBJ '{}' contains an invalid texture-coordinate index",
                        path.string()
                    );
                texCoordIndicesAliasVertices &= index.texcoord_index == index.position_index;
            }
        }
    }

    auto const numTriangles = triangleOffsets.back();
    if (numTriangles == 0)
        throw kira::Anyhow("TriangleMesh: OBJ '{}' contains no triangles", path.string());

    auto const hasCompleteNormals = anyNormalIndex && allCornersHaveNormals && numNormals != 0;
    auto const hasCompleteTexCoords =
        anyTexCoordIndex && allCornersHaveTexCoords && numTexCoords != 0;
    if (!hasCompleteNormals && (anyNormalIndex || numNormals != 0))
        LogWarn(
            "TriangleMesh: replacing incomplete normals in '{}' with angle-weighted vertex normals",
            path.string()
        );
    if (!hasCompleteTexCoords && (anyTexCoordIndex || numTexCoords != 0))
        LogWarn("TriangleMesh: ignoring incomplete texture coordinates in '{}'", path.string());

    // Release parsed arrays after their last use to limit peak memory.
    release(result.attributes.colors);
    if (!hasCompleteNormals)
        release(result.attributes.normals);
    if (!hasCompleteTexCoords)
        release(result.attributes.texcoords);

    triangles_.resize_for_overwrite(numTriangles);
    if (hasCompleteNormals && !normalIndicesAliasVertices)
        normalIndices_.resize_for_overwrite(numTriangles);
    if (hasCompleteTexCoords && !texCoordIndicesAliasVertices)
        texCoordIndices_.resize_for_overwrite(numTriangles);

    // Write each shape to its own output range in parallel.
    tbb::parallel_for(std::size_t{0}, result.shapes.size(), [&](std::size_t shapeIndex) {
        auto const &indices = result.shapes[shapeIndex].mesh.indices;
        auto const outputOffset = triangleOffsets[shapeIndex];
        for (std::size_t triangleIndex = 0; triangleIndex < indices.size() / 3; ++triangleIndex) {
            auto const sourceOffset = triangleIndex * 3;
            auto const destination = outputOffset + triangleIndex;
            triangles_[destination] = Vec3u{
                static_cast<std::uint32_t>(indices[sourceOffset].position_index),
                static_cast<std::uint32_t>(indices[sourceOffset + 1].position_index),
                static_cast<std::uint32_t>(indices[sourceOffset + 2].position_index),
            };
            if (!normalIndices_.empty())
                normalIndices_[destination] = Vec3u{
                    static_cast<std::uint32_t>(indices[sourceOffset].normal_index),
                    static_cast<std::uint32_t>(indices[sourceOffset + 1].normal_index),
                    static_cast<std::uint32_t>(indices[sourceOffset + 2].normal_index),
                };
            if (!texCoordIndices_.empty())
                texCoordIndices_[destination] = Vec3u{
                    static_cast<std::uint32_t>(indices[sourceOffset].texcoord_index),
                    static_cast<std::uint32_t>(indices[sourceOffset + 1].texcoord_index),
                    static_cast<std::uint32_t>(indices[sourceOffset + 2].texcoord_index),
                };
        }
    });
    release(result.shapes);

    vertices_.resize_for_overwrite(numVertices + 1);
    tbb::parallel_for(std::size_t{0}, numVertices, [&](std::size_t index) {
        auto const offset = index * 3;
        vertices_[index] = Vec3f{
            result.attributes.positions[offset],
            result.attributes.positions[offset + 1],
            result.attributes.positions[offset + 2],
        };
    });
    vertices_[numVertices] = Vec3f{};
    release(result.attributes.positions);

    if (hasCompleteNormals) {
        normals_.resize_for_overwrite(numNormals);
        tbb::parallel_for(std::size_t{0}, numNormals, [&](std::size_t index) {
            auto const offset = index * 3;
            normals_[index] = Vec3f{
                result.attributes.normals[offset],
                result.attributes.normals[offset + 1],
                result.attributes.normals[offset + 2],
            };
        });
        release(result.attributes.normals);
    } else {
        generateVertexNormals(getVertices(), getTriangles(), normals_);
    }

    if (hasCompleteTexCoords) {
        texCoords_.resize_for_overwrite(numTexCoords);
        tbb::parallel_for(std::size_t{0}, numTexCoords, [&](std::size_t index) {
            auto const offset = index * 2;
            texCoords_[index] = Vec2f{
                result.attributes.texcoords[offset],
                result.attributes.texcoords[offset + 1],
            };
        });
        release(result.attributes.texcoords);
    }
}
} // namespace flux
