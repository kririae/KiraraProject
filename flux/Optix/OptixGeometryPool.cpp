#include "flux/Optix/OptixGeometryPool.h"

#include <utility>

#include "kira/SmallVector.h"

namespace flux {
static_assert(sizeof(Vec3f) == 3 * sizeof(float));
static_assert(sizeof(Vec3u) == 3 * sizeof(std::uint32_t));

void OptixGeometryPool::add(std::span<Ref<TriangleMesh const> const> meshes) {
    if (meshes.empty())
        return;

    std::vector<Entry> entries;
    entries.reserve(meshes.size());
    for (auto const &mesh : meshes) {
        auto &entry = entries.emplace_back(getStream());

        auto const &data = mesh->getData();
        entry.vertices.copyFromHost(data.vertices, deletionQueue_);
        entry.triangles.copyFromHost(data.triangles, deletionQueue_);
        entry.normals.copyFromHost(data.normals, deletionQueue_);
        entry.normalIndices.copyFromHost(data.normalIndices, deletionQueue_);
        entry.texCoords.copyFromHost(data.texCoords, deletionQueue_);
        entry.texCoordIndices.copyFromHost(data.texCoordIndices, deletionQueue_);

        auto const hostImpl = mesh->getImpl();
        kira::SmallVector<float, 0> cdf;
        kira::SmallVector<float, 0> pdf;
        cdf.resize_for_overwrite(hostImpl.numTriangles);
        pdf.resize_for_overwrite(hostImpl.numTriangles);
        TriangleMesh::computeSamplingDistribution(
            hostImpl, {cdf.data(), cdf.size()}, {pdf.data(), pdf.size()}
        );

        entry.triangleAreaCDF.copyFromHost({cdf.data(), cdf.size()}, deletionQueue_);
        entry.triangleAreaPDF.copyFromHost({pdf.data(), pdf.size()}, deletionQueue_);
        entry.surfaceArea = cdf.empty() ? hostImpl.surfaceArea : cdf.back();
    }

    // Build every GAS in one batch. The inputs point into these locals.
    unsigned int const flags = OPTIX_GEOMETRY_FLAG_NONE;
    std::vector<CUdeviceptr> vertexBuffers;
    std::vector<OptixBuildInput> inputs;
    vertexBuffers.reserve(entries.size());
    inputs.reserve(entries.size());
    for (auto const &entry : entries) {
        vertexBuffers.push_back(devicePointer(entry.vertices.data()));
        OptixBuildInput input{
            .type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES,
            .triangleArray = {},
        };
        input.triangleArray.vertexBuffers = &vertexBuffers.back();
        input.triangleArray.numVertices = static_cast<unsigned int>(entry.vertices.size());
        input.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        input.triangleArray.vertexStrideInBytes = sizeof(Vec3f);
        input.triangleArray.indexBuffer = devicePointer(entry.triangles.data());
        input.triangleArray.numIndexTriplets = static_cast<unsigned int>(entry.triangles.size());
        input.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        input.triangleArray.indexStrideInBytes = sizeof(Vec3u);
        input.triangleArray.flags = &flags;
        input.triangleArray.numSbtRecords = 1;
        inputs.push_back(input);
    }
    auto gases = buildGas(deviceContext_, getStream(), inputs);

    entries_.reserve(entries_.size() + entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        entries[i].gas = std::move(gases[i]);
        entries_.try_emplace(meshes[i]->getContextId(), std::move(entries[i]));
    }
}

void OptixGeometryPool::remove(std::span<std::size_t const> ids) {
    for (auto const id : ids)
        entries_.erase(id);
}

std::vector<std::size_t> OptixGeometryPool::getIds() const {
    std::vector<std::size_t> ids;
    ids.reserve(entries_.size());
    for (auto const &[id, entry] : entries_)
        ids.push_back(id);
    return ids;
}

Geometry::Impl OptixGeometryPool::getImpl(std::size_t id) const {
    auto const &entry = entries_.at(id);

    // Pair the attributes with their indices as TriangleMesh::makeImpl does.
    auto const *normalIndices =
        entry.normalIndices.empty() ? entry.triangles.data() : entry.normalIndices.data();
    auto const *texCoordIndices =
        entry.texCoordIndices.empty() ? entry.triangles.data() : entry.texCoordIndices.data();
    return TriangleMesh::Impl{
        .vertices = entry.vertices.data(),
        .triangles = entry.triangles.data(),
        .normals = entry.normals.empty() ? nullptr : entry.normals.data(),
        .normalIndices = entry.normals.empty() ? nullptr : normalIndices,
        .texCoords = entry.texCoords.empty() ? nullptr : entry.texCoords.data(),
        .texCoordIndices = entry.texCoords.empty() ? nullptr : texCoordIndices,
        .numVertices = static_cast<std::uint32_t>(entry.vertices.size()),
        .numTriangles = static_cast<std::uint32_t>(entry.triangles.size()),
        .triangleAreaCDF = entry.triangleAreaCDF.data(),
        .triangleAreaPDF = entry.triangleAreaPDF.data(),
        .surfaceArea = entry.surfaceArea,
    };
}
} // namespace flux
