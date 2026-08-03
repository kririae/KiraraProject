#pragma once

#include <optix_types.h>

#include <cstdint>
#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/TriangleMesh.h"
#include "kira/SmallVector.h"

namespace flux {
/// \brief Owns the device storage used by OptiX triangle build inputs.
///
/// Uploads are ordered on one CUDA stream. Mutation is single-threaded;
/// callers provide external synchronization.
class OptixGeometryPool final : private Noncopyable, private CudaStreamMixin {
public:
    explicit OptixGeometryPool(cudaStream_t stream) noexcept
        : CudaStreamMixin(stream), deviceImpls_(stream) {}

    /// \brief Builds the resident triangle meshes.
    ///
    /// Meshes omitted from \p meshes are released.
    /// \param meshes Host meshes to upload in device-table order.
    /// \throw kira::Anyhow If CUDA cannot enqueue an allocation or copy.
    void build(std::span<Ref<TriangleMesh const> const> meshes);

    /// \brief Creates build inputs backed by the current resident storage.
    ///
    /// The returned inputs remain valid until the next call to \c build.
    [[nodiscard]] std::vector<OptixBuildInput> getBuildInputs() const;

    /// \brief Returns the number of resident meshes.
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    ///
    [[nodiscard]] Geometry::Impl const *getDeviceImpls() const noexcept {
        return deviceImpls_.data();
    }

private:
    struct Entry {
        explicit Entry(cudaStream_t stream)
            : vertices(stream), triangles(stream), normals(stream), normalIndices(stream),
              texCoords(stream), texCoordIndices(stream), triangleAreaCDF(stream),
              triangleAreaPDF(stream) {}

        DeviceBuffer<Vec3f> vertices;
        DeviceBuffer<Vec3u> triangles;
        DeviceBuffer<Vec3f> normals;
        DeviceBuffer<Vec3u> normalIndices;
        DeviceBuffer<Vec2f> texCoords;
        DeviceBuffer<Vec3u> texCoordIndices;
        kira::SmallVector<float, 0> triangleAreaCDFStaging;
        kira::SmallVector<float, 0> triangleAreaPDFStaging;
        DeviceBuffer<float> triangleAreaCDF;
        DeviceBuffer<float> triangleAreaPDF;
        CUdeviceptr vertexBuffer{};
        unsigned int flags{OPTIX_GEOMETRY_FLAG_NONE};
    };

    std::vector<Entry> entries_;
    std::vector<Geometry::Impl> staging_;
    DeviceBuffer<Geometry::Impl> deviceImpls_;
};
} // namespace flux
