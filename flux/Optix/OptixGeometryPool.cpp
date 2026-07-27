#include "flux/Optix/OptixGeometryPool.h"

#include <limits>

#include "flux/Optix/OptixUtils.h"
#include "kira/Anyhow.h"

namespace flux {
static_assert(sizeof(Vec3f) == 3 * sizeof(float));
static_assert(sizeof(Vec3u) == 3 * sizeof(std::uint32_t));

void OptixGeometryPool::upload(std::span<Ref<TriangleMesh const> const> meshes) {
    if (meshes.size() > std::numeric_limits<unsigned int>::max())
        throw kira::Anyhow("OptixGeometryPool: mesh count exceeds OptiX limits");

    deviceImpls_.clear();
    entries_.clear();
    staging_.clear();
    entries_.reserve(meshes.size());
    staging_.reserve(meshes.size());

    for (auto const &mesh : meshes) {
        auto const vertices = mesh->getVertices();
        auto const triangles = mesh->getTriangles();
        if (vertices.size() > std::numeric_limits<unsigned int>::max() ||
            triangles.size() > std::numeric_limits<unsigned int>::max())
            throw kira::Anyhow("OptixGeometryPool: triangle mesh exceeds OptiX limits");

        auto &entry = entries_.emplace_back(getStream());
        entry.vertices.copyFromHost({vertices.data(), vertices.size()});
        entry.triangles.copyFromHost({triangles.data(), triangles.size()});
        entry.vertexBuffer = devicePointer(entry.vertices.data());
        staging_.push_back({
            .vertices = entry.vertices.data(),
            .triangles = entry.triangles.data(),
            .numVertices = static_cast<std::uint32_t>(vertices.size()),
            .numTriangles = static_cast<std::uint32_t>(triangles.size()),
        });
    }

    deviceImpls_.copyFromHost({staging_.data(), staging_.size()});
}

std::vector<OptixBuildInput> OptixGeometryPool::getBuildInputs() const {
    std::vector<OptixBuildInput> inputs;
    inputs.reserve(entries_.size());

    for (auto const &entry : entries_) {
        OptixBuildInput input{
            .type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES,
            .triangleArray = {},
        };
        input.triangleArray.vertexBuffers = &entry.vertexBuffer;
        input.triangleArray.numVertices = static_cast<unsigned int>(entry.vertices.size());
        input.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        input.triangleArray.vertexStrideInBytes = sizeof(Vec3f);
        input.triangleArray.indexBuffer = devicePointer(entry.triangles.data());
        input.triangleArray.numIndexTriplets = static_cast<unsigned int>(entry.triangles.size());
        input.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        input.triangleArray.indexStrideInBytes = sizeof(Vec3u);
        input.triangleArray.flags = &entry.flags;
        input.triangleArray.numSbtRecords = 1;
        inputs.push_back(input);
    }
    return inputs;
}
} // namespace flux
