#pragma once

#include <optix_types.h>

#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeletionQueue.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixGas.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/TriangleMesh.h"

namespace flux {
/// \brief Device geometry of triangle meshes by Context ID: the arrays and the GAS of each.
///
/// A mesh is immutable and an ID never repeats, so an entry stays valid until its mesh is
/// removed. The pool knows no geometry index. Its owner places each mesh in a table. Work is
/// ordered on one CUDA stream. Mutation is single-threaded, so callers provide external
/// synchronization.
class OptixGeometryPool final : private Noncopyable, private CudaStreamMixin {
public:
    OptixGeometryPool(OptixDeviceContext deviceContext, DeletionQueue &deletionQueue) noexcept
        : CudaStreamMixin(deletionQueue.getStream()), deviceContext_(deviceContext),
          deletionQueue_(deletionQueue) {}

    /// \brief Uploads \p meshes and builds their GASes in one batch.
    /// \pre No mesh of \p meshes is present, and their IDs are distinct.
    /// \throw kira::Anyhow If a mesh exceeds the device limit, or OptiX or CUDA fails.
    void add(std::span<Ref<TriangleMesh const> const> meshes);

    /// \brief Drops the meshes \p ids with their arrays and GASes.
    /// \pre Every ID of \p ids is present.
    void remove(std::span<std::size_t const> ids);

    /// \brief Returns whether the mesh \p id is present.
    [[nodiscard]] bool contains(std::size_t id) const noexcept { return entries_.contains(id); }

    /// \brief Returns the ID of every present mesh, in no particular order.
    [[nodiscard]] std::vector<std::size_t> getIds() const;

    /// \brief Assembles the device Impl of the mesh \p id from its arrays.
    /// \throw std::out_of_range If the mesh is absent.
    [[nodiscard]] Geometry::Impl getImpl(std::size_t id) const;

    /// \brief Returns the GAS of the mesh \p id.
    /// \throw std::out_of_range If the mesh is absent.
    [[nodiscard]] OptixTraversableHandle getGas(std::size_t id) const {
        return entries_.at(id).gas.handle;
    }

private:
    /// Device storage of one mesh.
    struct Entry {
        explicit Entry(cudaStream_t stream)
            : vertices(stream), triangles(stream), normals(stream), normalIndices(stream),
              texCoords(stream), texCoordIndices(stream), triangleAreaBins(stream),
              triangleAreaPDF(stream), gas(stream) {}

        DeviceBuffer<Vec3f> vertices;
        DeviceBuffer<Vec3u> triangles;
        DeviceBuffer<Vec3f> normals;
        DeviceBuffer<Vec3u> normalIndices;
        DeviceBuffer<Vec2f> texCoords;
        DeviceBuffer<Vec3u> texCoordIndices;
        DeviceBuffer<AliasBin> triangleAreaBins;
        DeviceBuffer<float> triangleAreaPDF;
        float surfaceArea{};
        OptixGas gas;
    };

    OptixDeviceContext deviceContext_;
    DeletionQueue &deletionQueue_;
    std::unordered_map<std::size_t, Entry> entries_;
};
} // namespace flux
