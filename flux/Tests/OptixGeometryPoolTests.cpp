#include <cuda_runtime_api.h>
#include <gtest/gtest.h>
#include <optix_stubs.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestUtils.h"
#include "flux/Optix/OptixGeometryPool.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/TriangleMesh.h"

namespace {
using flux::Ref;
using flux::TriangleMesh;

[[nodiscard]] kira::Properties meshProperties(std::string const &name) {
    kira::Properties properties;
    properties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / name);
    return properties;
}

/// Owns a CUDA stream and an OptiX context for one test.
class Device {
public:
    Device() {
        flux::cudaCheck(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
        flux::optixCheck(optixInit());
        OptixDeviceContextOptions options{};
        flux::optixCheck(optixDeviceContextCreate(nullptr, &options, &context));
    }
    Device(Device const &) = delete;
    Device &operator=(Device const &) = delete;
    ~Device() {
        flux::optixCheck<false>(optixDeviceContextDestroy(context));
        flux::cudaCheck<false>(cudaStreamDestroy(stream));
    }

    cudaStream_t stream{};
    OptixDeviceContext context{};
};

/// Requires the device array \p got to hold the \p count elements of the host array \p want, and
/// to be null exactly when \p want is.
template <typename T>
void expectArray(char const *name, T const *got, T const *want, std::size_t count) {
    SCOPED_TRACE(name);
    ASSERT_EQ(got == nullptr, want == nullptr);
    if (!want)
        return;
    std::vector<T> data(count);
    flux::cudaCheck(cudaMemcpy(data.data(), got, count * sizeof(T), cudaMemcpyDeviceToHost));
    EXPECT_EQ(std::memcmp(data.data(), want, count * sizeof(T)), 0);
}

/// Requires the device Impl to hold the arrays of \p mesh, paired as the host Impl pairs them.
void expectSameMesh(flux::Geometry::Impl const &device, TriangleMesh const &mesh) {
    auto const &got = cuda::std::get<TriangleMesh::Impl>(device);
    auto const want = mesh.getImpl();
    ASSERT_EQ(got.numVertices, want.numVertices);
    ASSERT_EQ(got.numTriangles, want.numTriangles);
    expectArray("vertices", got.vertices, want.vertices, want.numVertices);
    expectArray("triangles", got.triangles, want.triangles, want.numTriangles);
    expectArray("normals", got.normals, want.normals, mesh.getNormals().size());
    expectArray("normalIndices", got.normalIndices, want.normalIndices, want.numTriangles);
    expectArray("texCoords", got.texCoords, want.texCoords, mesh.getTexCoords().size());
    expectArray("texCoordIndices", got.texCoordIndices, want.texCoordIndices, want.numTriangles);

    // An attribute without its own indices shares the triangles.
    EXPECT_EQ(got.normalIndices == got.triangles, want.normalIndices == want.triangles);
    EXPECT_EQ(got.texCoordIndices == got.triangles, want.texCoordIndices == want.triangles);

    std::vector<float> cdf(want.numTriangles), pdf(want.numTriangles);
    TriangleMesh::computeSamplingDistribution(want, cdf, pdf);
    expectArray("triangleAreaCDF", got.triangleAreaCDF, cdf.data(), cdf.size());
    expectArray("triangleAreaPDF", got.triangleAreaPDF, pdf.data(), pdf.size());
    EXPECT_FLOAT_EQ(got.surfaceArea, want.surfaceArea);
}
} // namespace

TEST(OptixGeometryPoolTests, AddsAndRemovesMeshesInBatches) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    Device device;
    auto context = flux::Context::create();
    std::array<Ref<TriangleMesh const>, 2> const meshes{
        context->create<TriangleMesh>(meshProperties("Triangle.obj")),
        context->create<TriangleMesh>(meshProperties("IndexedTriangle.obj")),
    };
    auto const plain = meshes[0]->getContextId();
    auto const indexed = meshes[1]->getContextId();

    flux::OptixGeometryPool pool(device.context, device.stream);
    pool.add(meshes);
    EXPECT_TRUE(pool.contains(plain));
    EXPECT_TRUE(pool.contains(indexed));
    EXPECT_EQ(pool.getIds().size(), 2U);
    EXPECT_NE(pool.getGas(plain), OptixTraversableHandle{});
    EXPECT_NE(pool.getGas(indexed), OptixTraversableHandle{});
    expectSameMesh(pool.getImpl(plain), *meshes[0]);
    expectSameMesh(pool.getImpl(indexed), *meshes[1]);

    std::array const removed{plain};
    pool.remove(removed);
    EXPECT_FALSE(pool.contains(plain));
    EXPECT_TRUE(pool.contains(indexed));
    EXPECT_THROW((void)pool.getImpl(plain), std::out_of_range);
    EXPECT_THROW((void)pool.getGas(plain), std::out_of_range);
    flux::cudaCheck(cudaStreamSynchronize(device.stream));
}
