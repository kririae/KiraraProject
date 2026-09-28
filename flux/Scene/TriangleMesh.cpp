#include "flux/Scene/TriangleMesh.h"

#include <miniply.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/parallel_scan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ranges>
#include <rapidobj/rapidobj.hpp>
#include <string>
#include <type_traits>
#include <vector>

#include "flux/Core/Logging.h"
#include "flux/Core/MathUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/TriangleMeshImpl.h"
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
        auto const &vertex0 = vertices[triangle[0u]];
        auto const &vertex1 = vertices[triangle[1u]];
        auto const &vertex2 = vertices[triangle[2u]];
        auto const faceVector = cross(vertex1 - vertex0, vertex2 - vertex0);
        auto const faceLengthSquared = faceVector.norm2();
        if (!(faceLengthSquared > 0.0F))
            return;
        auto const faceNormal = faceVector / std::sqrt(faceLengthSquared);

        for (std::size_t corner = 0; corner < 3; ++corner) {
            auto const &center = vertices[triangle[corner]];
            auto previous = vertices[triangle[(corner + 2) % 3]] - center;
            auto next = vertices[triangle[(corner + 1) % 3]] - center;
            auto const previousLengthSquared = previous.norm2();
            auto const nextLengthSquared = next.norm2();
            if (!(previousLengthSquared > 0.0F) || !(nextLengthSquared > 0.0F))
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
        if (lengthSquared > 0.0F)
            normals[index] = normals[index] / std::sqrt(lengthSquared);
    });
}

/// \brief Returns the largest index in \p indices, or zero when it is empty.
[[nodiscard]] std::uint32_t largestIndex(std::span<Vec3u const> indices) {
    return tbb::parallel_reduce(
        tbb::blocked_range<std::size_t>{0, indices.size()}, std::uint32_t{0},
        [&](auto const &range, std::uint32_t largest) {
        for (auto index = range.begin(); index != range.end(); ++index)
            largest =
                std::max({largest, indices[index][0u], indices[index][1u], indices[index][2u]});
        return largest;
    }, [](std::uint32_t left, std::uint32_t right) { return std::max(left, right); }
    );
}

[[nodiscard]] TriangleMesh::Data loadObj(std::filesystem::path const &path);
[[nodiscard]] TriangleMesh::Data loadPly(std::filesystem::path const &path);

/// \brief Loads the mesh file named by the \c path property of \p props.
[[nodiscard]] TriangleMesh::Data
loadMeshFile(kira::FileResolver const &resolver, kira::Properties const &props) {
    auto const authoredPath = props.use<std::filesystem::path>("path");
    auto const path = resolver.resolve(authoredPath);
    if (!std::filesystem::exists(path))
        throw kira::Anyhow("TriangleMesh: failed to resolve '{}'", authoredPath.string());

    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (extension == ".obj")
        return loadObj(path);
    if (extension == ".ply")
        return loadPly(path);
    throw kira::Anyhow(
        "TriangleMesh: unsupported file extension '{}' for '{}'", extension, path.string()
    );
}
} // namespace

TriangleMesh::TriangleMesh(TXContext &tx, kira::Properties const &props)
    : TriangleMesh(tx, loadMeshFile(tx.getContext().getFileResolver(), props)) {}

TriangleMesh::TriangleMesh(TXContext &tx, Data &&data)
    : Geometry(tx, GeometryType::TriangleMesh), vertices_(std::move(data.vertices)),
      triangles_(std::move(data.triangles)), normals_(std::move(data.normals)),
      normalIndices_(std::move(data.normalIndices)), texCoords_(std::move(data.texCoords)),
      texCoordIndices_(std::move(data.texCoordIndices)) {
    if (vertices_.size() >= std::numeric_limits<std::uint32_t>::max())
        throw kira::Anyhow(
            "TriangleMesh: {} vertices exceed the addressable range", vertices_.size()
        );
    vertices_.push_back(Vec3f{});

    if (normals_.empty())
        generateVertexNormals(getVertices(), getTriangles(), normals_);

    auto const impl = getImpl();
    surfaceArea_ = tbb::parallel_reduce(
        tbb::blocked_range<std::uint32_t>{0, impl.numTriangles}, 0.0F,
        [&](auto const &range, float area) {
        for (auto triangle = range.begin(); triangle != range.end(); ++triangle)
            area += impl.getTriangleArea(triangle);
        return area;
    }, std::plus<>{}
    );
}

void TriangleMesh::checkIndices(Data const &data) {
    auto const check = [&](std::span<Vec3u const> indices, std::size_t limit, char const *name) {
        if (indices.empty())
            return;
        if (indices.size() != data.triangles.size())
            throw kira::Anyhow(
                "TriangleMesh: {} {} indices do not match {} triangles", indices.size(), name,
                data.triangles.size()
            );

        auto const largest = largestIndex(indices);
        if (largest >= limit)
            throw kira::Anyhow(
                "TriangleMesh: a triangle addresses {} {} of {}", name, largest, limit
            );
    };

    auto const triangles = std::span<Vec3u const>{data.triangles};

    // Indices without their attribute array would be published against generated
    // normals, which are per vertex, or against no array at all.
    auto const checkAttribute = [&](std::span<Vec3u const> indices, std::size_t attributeCount,
                                    char const *name) {
        if (attributeCount == 0) {
            if (!indices.empty())
                throw kira::Anyhow(
                    "TriangleMesh: {} {} indices address no {}", indices.size(), name, name
                );
            return;
        }
        check(indices.empty() ? triangles : indices, attributeCount, name);
    };

    check(triangles, data.vertices.size(), "vertex");
    checkAttribute(data.normalIndices, data.normals.size(), "normal");
    checkAttribute(data.texCoordIndices, data.texCoords.size(), "texture-coordinate");
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
        .surfaceArea = surfaceArea_,
    };
}

void TriangleMesh::computeSamplingDistribution(
    Impl const &mesh, std::span<float> areaCDF, std::span<float> areaPDF
) {
    KIRA_ASSERT(
        areaCDF.size() == mesh.numTriangles && areaPDF.size() == mesh.numTriangles,
        "Triangle sampling arrays do not match the mesh"
    );

    tbb::parallel_for(std::uint32_t{0}, mesh.numTriangles, [&](std::uint32_t triangle) {
        areaPDF[triangle] = mesh.getTriangleArea(triangle);
    });
    tbb::parallel_scan(
        tbb::blocked_range<std::uint32_t>{0, mesh.numTriangles}, 0.0,
        [&](auto const &range, double area, bool const isFinalScan) {
        for (auto triangle = range.begin(); triangle != range.end(); ++triangle) {
            area += areaPDF[triangle];
            if (isFinalScan)
                areaCDF[triangle] = static_cast<float>(area);
        }
        return area;
    }, std::plus<>{}
    );
    if (areaCDF.empty() || areaCDF.back() == 0.0F) {
        std::ranges::fill(areaPDF, 0.0F);
        return;
    }

    auto const cdfTotal = areaCDF.back();
    tbb::parallel_for(std::size_t{0}, areaPDF.size(), [&](std::size_t index) {
        auto const previous = index == 0 ? 0.0F : areaCDF[index - 1];
        auto const interval = areaCDF[index] - previous;
        auto const area = areaPDF[index];
        areaPDF[index] =
            area == 0.0F
                ? 0.0F
                : static_cast<float>(
                      static_cast<double>(interval) / (static_cast<double>(cdfTotal) * area)
                  );
    });
}

namespace {
TriangleMesh::Data loadObj(std::filesystem::path const &path) {
    TriangleMesh::Data data;
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
    if (numVertices >= std::numeric_limits<std::uint32_t>::max())
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

    data.triangles.resize_for_overwrite(numTriangles);
    if (hasCompleteNormals && !normalIndicesAliasVertices)
        data.normalIndices.resize_for_overwrite(numTriangles);
    if (hasCompleteTexCoords && !texCoordIndicesAliasVertices)
        data.texCoordIndices.resize_for_overwrite(numTriangles);

    // Write each shape to its own output range in parallel.
    tbb::parallel_for(std::size_t{0}, result.shapes.size(), [&](std::size_t shapeIndex) {
        auto const &indices = result.shapes[shapeIndex].mesh.indices;
        auto const outputOffset = triangleOffsets[shapeIndex];
        for (std::size_t triangleIndex = 0; triangleIndex < indices.size() / 3; ++triangleIndex) {
            auto const sourceOffset = triangleIndex * 3;
            auto const destination = outputOffset + triangleIndex;
            data.triangles[destination] = Vec3u{
                static_cast<std::uint32_t>(indices[sourceOffset].position_index),
                static_cast<std::uint32_t>(indices[sourceOffset + 1].position_index),
                static_cast<std::uint32_t>(indices[sourceOffset + 2].position_index),
            };
            if (!data.normalIndices.empty())
                data.normalIndices[destination] = Vec3u{
                    static_cast<std::uint32_t>(indices[sourceOffset].normal_index),
                    static_cast<std::uint32_t>(indices[sourceOffset + 1].normal_index),
                    static_cast<std::uint32_t>(indices[sourceOffset + 2].normal_index),
                };
            if (!data.texCoordIndices.empty())
                data.texCoordIndices[destination] = Vec3u{
                    static_cast<std::uint32_t>(indices[sourceOffset].texcoord_index),
                    static_cast<std::uint32_t>(indices[sourceOffset + 1].texcoord_index),
                    static_cast<std::uint32_t>(indices[sourceOffset + 2].texcoord_index),
                };
        }
    });
    release(result.shapes);

    data.vertices.reserve(numVertices + 1);
    data.vertices.resize_for_overwrite(numVertices);
    tbb::parallel_for(std::size_t{0}, numVertices, [&](std::size_t index) {
        auto const offset = index * 3;
        data.vertices[index] = Vec3f{
            result.attributes.positions[offset],
            result.attributes.positions[offset + 1],
            result.attributes.positions[offset + 2],
        };
    });
    release(result.attributes.positions);

    if (hasCompleteNormals) {
        data.normals.resize_for_overwrite(numNormals);
        tbb::parallel_for(std::size_t{0}, numNormals, [&](std::size_t index) {
            auto const offset = index * 3;
            data.normals[index] = Vec3f{
                result.attributes.normals[offset],
                result.attributes.normals[offset + 1],
                result.attributes.normals[offset + 2],
            };
        });
        release(result.attributes.normals);
    }

    if (hasCompleteTexCoords) {
        data.texCoords.resize_for_overwrite(numTexCoords);
        tbb::parallel_for(std::size_t{0}, numTexCoords, [&](std::size_t index) {
            auto const offset = index * 2;
            data.texCoords[index] = Vec2f{
                result.attributes.texcoords[offset],
                result.attributes.texcoords[offset + 1],
            };
        });
        release(result.attributes.texcoords);
    }
    return data;
}

TriangleMesh::Data loadPly(std::filesystem::path const &path) {
    TriangleMesh::Data data;
    static_assert(sizeof(Vec2f) == 2 * sizeof(float));
    static_assert(sizeof(Vec3f) == 3 * sizeof(float));
    static_assert(sizeof(Vec3u) == 3 * sizeof(std::uint32_t));
    static_assert(std::is_trivially_copyable_v<Vec2f>);
    static_assert(std::is_trivially_copyable_v<Vec3f>);
    static_assert(std::is_trivially_copyable_v<Vec3u>);

    auto const pathString = path.string();
    bool hasVertices = false;
    bool hasTriangles = false;
    bool reloadFaces = false;

    auto loadFaces = [&](miniply::PLYReader &reader) {
        std::uint32_t faceProperty;
        if (!reader.find_indices(&faceProperty))
            throw kira::Anyhow("TriangleMesh: PLY '{}' contains no vertex indices", pathString);

        auto const &property = reader.element()->properties[faceProperty];
        if (property.type >= miniply::PLYPropertyType::Float ||
            property.countType >= miniply::PLYPropertyType::Float)
            throw kira::Anyhow(
                "TriangleMesh: PLY '{}' has an unsupported vertex-index list type", pathString
            );
        if (!reader.load_element() || !reader.valid())
            throw kira::Anyhow("TriangleMesh: failed to read faces from PLY '{}'", pathString);

        auto const *counts = reader.get_list_counts(faceProperty);
        if (!counts)
            throw kira::Anyhow("TriangleMesh: PLY '{}' vertex indices are not a list", pathString);

        std::uint64_t numTriangles = 0;
        std::uint64_t numIndices = 0;
        bool needsTriangulation = false;
        for (std::uint32_t face = 0; face < reader.num_rows(); ++face) {
            auto const count = counts[face];
            if (count < 3)
                throw kira::Anyhow(
                    "TriangleMesh: PLY '{}' contains a face with fewer than three vertices",
                    pathString
                );
            numTriangles += count - 2;
            numIndices += count;
            needsTriangulation |= count != 3;
        }
        if (numTriangles == 0)
            throw kira::Anyhow("TriangleMesh: PLY '{}' contains no triangles", pathString);
        if (numTriangles > std::numeric_limits<std::uint32_t>::max())
            throw kira::Anyhow("TriangleMesh: PLY '{}' has too many triangles", pathString);
        if (needsTriangulation && numIndices > std::numeric_limits<std::uint32_t>::max())
            throw kira::Anyhow("TriangleMesh: PLY '{}' has too many polygon indices", pathString);

        if (!needsTriangulation) {
            data.triangles.resize_for_overwrite(static_cast<std::size_t>(numTriangles));
            if (!reader.extract_list_property(
                    faceProperty, miniply::PLYPropertyType::UInt, data.triangles.front().data()
                ))
                throw kira::Anyhow("TriangleMesh: failed to read faces from PLY '{}'", pathString);
            return;
        }

        kira::SmallVector<std::uint32_t, 0> polygonIndices;
        polygonIndices.resize_for_overwrite(static_cast<std::size_t>(numIndices));
        if (!reader.extract_list_property(
                faceProperty, miniply::PLYPropertyType::UInt, polygonIndices.data()
            ))
            throw kira::Anyhow(
                "TriangleMesh: failed to read vertex indices from PLY '{}'", pathString
            );
        if (!std::ranges::all_of(polygonIndices, [&](std::uint32_t index) {
            return index < data.vertices.size();
        }))
            throw kira::Anyhow(
                "TriangleMesh: PLY '{}' contains an invalid vertex index", pathString
            );

        std::array<std::vector<std::array<float, 2>>, 1> projectedPolygon;
        std::size_t sourceOffset = 0;
        data.triangles.reserve(static_cast<std::size_t>(numTriangles));
        for (std::uint32_t face = 0; face < reader.num_rows(); ++face) {
            auto const count = counts[face];
            if (count == 3) {
                data.triangles.push_back(
                    Vec3u{
                        polygonIndices[sourceOffset],
                        polygonIndices[sourceOffset + 1],
                        polygonIndices[sourceOffset + 2],
                    }
                );
            } else {
                Vec3f polygonNormal{};
                for (std::uint32_t corner = 0; corner < count; ++corner) {
                    auto const &current = data.vertices[polygonIndices[sourceOffset + corner]];
                    auto const &next =
                        data.vertices[polygonIndices[sourceOffset + (corner + 1) % count]];
                    polygonNormal.x() += (current.y() - next.y()) * (current.z() + next.z());
                    polygonNormal.y() += (current.z() - next.z()) * (current.x() + next.x());
                    polygonNormal.z() += (current.x() - next.x()) * (current.y() + next.y());
                }

                auto const normalLengthSquared = polygonNormal.norm2();
                if (!(normalLengthSquared > 0.0F))
                    throw kira::Anyhow(
                        "TriangleMesh: failed to triangulate a degenerate face in PLY '{}'",
                        pathString
                    );

                // Project the planar face along its dominant normal axis.
                auto projectionAxis = std::size_t{0};
                if (std::abs(polygonNormal.y()) > std::abs(polygonNormal.x()))
                    projectionAxis = 1;
                if (std::abs(polygonNormal.z()) > std::abs(polygonNormal[projectionAxis]))
                    projectionAxis = 2;

                auto &polygon = projectedPolygon.front();
                polygon.clear();
                polygon.reserve(count);
                for (std::uint32_t corner = 0; corner < count; ++corner) {
                    auto const &position = data.vertices[polygonIndices[sourceOffset + corner]];
                    if (projectionAxis == 0)
                        polygon.push_back({position.y(), position.z()});
                    else if (projectionAxis == 1)
                        polygon.push_back({position.x(), position.z()});
                    else
                        polygon.push_back({position.x(), position.y()});
                }

                auto const localTriangles = mapbox::earcut(projectedPolygon);
                if (localTriangles.empty() || localTriangles.size() % 3 != 0 ||
                    localTriangles.size() > static_cast<std::uint64_t>(count - 2) * 3)
                    throw kira::Anyhow(
                        "TriangleMesh: failed to triangulate a face in PLY '{}'", pathString
                    );
                for (std::size_t triangle = 0; triangle < localTriangles.size(); triangle += 3) {
                    auto index0 = localTriangles[triangle];
                    auto index1 = localTriangles[triangle + 1];
                    auto index2 = localTriangles[triangle + 2];
                    if (index0 >= count || index1 >= count || index2 >= count)
                        throw kira::Anyhow(
                            "TriangleMesh: triangulation produced an invalid index for PLY '{}'",
                            pathString
                        );

                    auto const &position0 = data.vertices[polygonIndices[sourceOffset + index0]];
                    auto const &position1 = data.vertices[polygonIndices[sourceOffset + index1]];
                    auto const &position2 = data.vertices[polygonIndices[sourceOffset + index2]];
                    if (cross(position1 - position0, position2 - position0).dot(polygonNormal) <
                        0.0F)
                        std::swap(index1, index2);
                    data.triangles.push_back(
                        Vec3u{
                            polygonIndices[sourceOffset + index0],
                            polygonIndices[sourceOffset + index1],
                            polygonIndices[sourceOffset + index2],
                        }
                    );
                }
            }
            sourceOffset += count;
        }
    };

    miniply::PLYReader reader{pathString.c_str()};
    if (!reader.valid())
        throw kira::Anyhow("TriangleMesh: failed to read PLY header '{}'", pathString);

    while (reader.has_element() && (!hasVertices || !hasTriangles)) {
        if (reader.element_is(miniply::kPLYVertexElement)) {
            auto const *element = reader.element();
            auto const hasScalarProperties = [&](auto const &properties) {
                return std::ranges::all_of(properties, [&](std::uint32_t property) {
                    return element->properties[property].countType ==
                           miniply::PLYPropertyType::None;
                });
            };

            std::array<std::uint32_t, 3> positionProperties;
            if (!reader.find_pos(positionProperties.data()) ||
                !hasScalarProperties(positionProperties))
                throw kira::Anyhow("TriangleMesh: PLY '{}' contains no valid vertices", pathString);

            std::array<std::uint32_t, 3> normalProperties;
            auto const hasNormals = reader.find_normal(normalProperties.data());
            if (hasNormals && !hasScalarProperties(normalProperties))
                throw kira::Anyhow(
                    "TriangleMesh: PLY '{}' contains non-scalar normals", pathString
                );
            auto const hasAnyNormal = element->find_property("nx") != miniply::kInvalidIndex ||
                                      element->find_property("ny") != miniply::kInvalidIndex ||
                                      element->find_property("nz") != miniply::kInvalidIndex;

            std::array<std::uint32_t, 2> texCoordProperties;
            auto const hasTexCoords = reader.find_texcoord(texCoordProperties.data());
            if (hasTexCoords && !hasScalarProperties(texCoordProperties))
                throw kira::Anyhow(
                    "TriangleMesh: PLY '{}' contains non-scalar texture coordinates", pathString
                );
            auto const hasAnyTexCoord =
                element->find_property("u") != miniply::kInvalidIndex ||
                element->find_property("v") != miniply::kInvalidIndex ||
                element->find_property("s") != miniply::kInvalidIndex ||
                element->find_property("t") != miniply::kInvalidIndex ||
                element->find_property("texture_u") != miniply::kInvalidIndex ||
                element->find_property("texture_v") != miniply::kInvalidIndex ||
                element->find_property("texture_s") != miniply::kInvalidIndex ||
                element->find_property("texture_t") != miniply::kInvalidIndex;

            if (!reader.load_element() || !reader.valid())
                throw kira::Anyhow(
                    "TriangleMesh: failed to read vertices from PLY '{}'", pathString
                );

            auto const numVertices = reader.num_rows();
            if (numVertices == 0)
                throw kira::Anyhow("TriangleMesh: PLY '{}' contains no vertices", pathString);
            if (numVertices == std::numeric_limits<std::uint32_t>::max())
                throw kira::Anyhow("TriangleMesh: PLY '{}' has too many vertices", pathString);

            data.vertices.reserve(static_cast<std::size_t>(numVertices) + 1);
            data.vertices.resize_for_overwrite(numVertices);
            if (!reader.extract_properties_with_stride(
                    positionProperties.data(),
                    static_cast<std::uint32_t>(positionProperties.size()),
                    miniply::PLYPropertyType::Float, data.vertices.front().data(), sizeof(Vec3f)
                ))
                throw kira::Anyhow(
                    "TriangleMesh: failed to read vertices from PLY '{}'", pathString
                );

            if (hasNormals) {
                data.normals.resize_for_overwrite(numVertices);
                if (!reader.extract_properties_with_stride(
                        normalProperties.data(),
                        static_cast<std::uint32_t>(normalProperties.size()),
                        miniply::PLYPropertyType::Float, data.normals.front().data(), sizeof(Vec3f)
                    ))
                    throw kira::Anyhow(
                        "TriangleMesh: failed to read normals from PLY '{}'", pathString
                    );
            } else if (hasAnyNormal) {
                LogWarn(
                    "TriangleMesh: replacing incomplete normals in '{}' with angle-weighted vertex "
                    "normals",
                    pathString
                );
            }

            if (hasTexCoords) {
                data.texCoords.resize_for_overwrite(numVertices);
                if (!reader.extract_properties_with_stride(
                        texCoordProperties.data(),
                        static_cast<std::uint32_t>(texCoordProperties.size()),
                        miniply::PLYPropertyType::Float, data.texCoords.front().data(),
                        sizeof(Vec2f)
                    ))
                    throw kira::Anyhow(
                        "TriangleMesh: failed to read texture coordinates from PLY '{}'", pathString
                    );
            } else if (hasAnyTexCoord) {
                LogWarn(
                    "TriangleMesh: ignoring incomplete texture coordinates in '{}'", pathString
                );
            }
            hasVertices = true;
        } else if (reader.element_is(miniply::kPLYFaceElement)) {
            if (hasVertices) {
                loadFaces(reader);
                hasTriangles = true;
            } else {
                reloadFaces = true;
            }
        }
        reader.next_element();
    }

    if (!hasVertices)
        throw kira::Anyhow("TriangleMesh: PLY '{}' contains no vertex element", pathString);

    if (reloadFaces && !hasTriangles) {
        miniply::PLYReader faceReader{pathString.c_str()};
        if (!faceReader.valid())
            throw kira::Anyhow("TriangleMesh: failed to reopen PLY '{}'", pathString);
        while (faceReader.has_element() && !faceReader.element_is(miniply::kPLYFaceElement))
            faceReader.next_element();
        if (!faceReader.has_element())
            throw kira::Anyhow("TriangleMesh: PLY '{}' contains no face element", pathString);
        loadFaces(faceReader);
        hasTriangles = true;
    }

    if (!hasTriangles)
        throw kira::Anyhow("TriangleMesh: PLY '{}' contains no triangles", pathString);

    // A face element of triangles is read without inspecting its indices.
    TriangleMesh::checkIndices(data);
    return data;
}
} // namespace
} // namespace flux
