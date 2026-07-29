#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <utility>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/TriangleMeshImpl.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

namespace {
struct ReconstructTriangleInteraction {
    flux::TriangleMesh::Impl geometry;
    flux::PreliminaryIntersection preliminary;
    flux::GeometryInteraction *result;

    KIRA_DEVICE void operator()(std::size_t) const noexcept {
        *result = geometry.computeInteraction(preliminary);
    }
};

[[nodiscard]] kira::Properties triangleProperties(char const *filename) {
    kira::Properties properties;
    properties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / filename);
    return properties;
}
} // namespace

TEST(GeometryTests, LoadsObjAndGeneratesMissingNormals) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties("BentQuad.obj"));

    ASSERT_EQ(mesh->getVertices().size(), 4);
    ASSERT_EQ(mesh->getTriangles().size(), 2);
    EXPECT_EQ(mesh->getTriangles()[0], (flux::Vec3u{0, 1, 2}));
    EXPECT_EQ(mesh->getTriangles()[1], (flux::Vec3u{0, 3, 1}));
    ASSERT_EQ(mesh->getNormals().size(), mesh->getVertices().size());
    EXPECT_TRUE(mesh->getNormalIndices().empty());
    EXPECT_TRUE(mesh->getTexCoords().empty());
    EXPECT_TRUE(mesh->getTexCoordIndices().empty());

    auto constexpr diagonal = 0.70710678F;
    EXPECT_EQ(mesh->getNormals()[2], (flux::Vec3f{0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(mesh->getNormals()[3], (flux::Vec3f{0.0F, 1.0F, 0.0F}));
    for (std::size_t index = 0; index < 2; ++index) {
        EXPECT_NEAR(mesh->getNormals()[index].x(), 0.0F, 1.0e-6F);
        EXPECT_NEAR(mesh->getNormals()[index].y(), diagonal, 1.0e-6F);
        EXPECT_NEAR(mesh->getNormals()[index].z(), diagonal, 1.0e-6F);
    }
}

TEST(GeometryTests, PreservesIndependentObjAttributeIndices) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties("IndexedTriangle.obj"));

    ASSERT_EQ(mesh->getNormals().size(), 3);
    ASSERT_EQ(mesh->getNormalIndices().size(), 1);
    EXPECT_EQ(mesh->getNormalIndices()[0], (flux::Vec3u{1, 2, 0}));
    EXPECT_EQ(mesh->getNormals()[0], (flux::Vec3f{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(mesh->getNormals()[1], (flux::Vec3f{0.0F, 1.0F, 0.0F}));
    EXPECT_EQ(mesh->getNormals()[2], (flux::Vec3f{0.0F, 0.0F, 1.0F}));

    ASSERT_EQ(mesh->getTexCoords().size(), 3);
    ASSERT_EQ(mesh->getTexCoordIndices().size(), 1);
    EXPECT_EQ(mesh->getTexCoordIndices()[0], (flux::Vec3u{2, 0, 1}));
    EXPECT_EQ(mesh->getTexCoords()[0], (flux::Vec2f{0.1F, 0.2F}));
    EXPECT_EQ(mesh->getTexCoords()[1], (flux::Vec2f{0.3F, 0.4F}));
    EXPECT_EQ(mesh->getTexCoords()[2], (flux::Vec2f{0.5F, 0.6F}));
}

TEST(GeometryTests, ReconstructsTriangleInteractionInGeometrySpace) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    std::array const vertices{
        flux::Vec3f{0.0F, 0.0F, 0.0F},
        flux::Vec3f{2.0F, 0.0F, 0.0F},
        flux::Vec3f{0.0F, 4.0F, 0.0F},
    };
    std::array const triangles{flux::Vec3u{0, 1, 2}};
    flux::DeviceBuffer<flux::Vec3f> deviceVertices(cudaStreamPerThread);
    flux::DeviceBuffer<flux::Vec3u> deviceTriangles(cudaStreamPerThread);
    flux::DeviceBuffer<flux::GeometryInteraction> deviceResult(cudaStreamPerThread);
    deviceVertices.copyFromHost(vertices);
    deviceTriangles.copyFromHost(triangles);
    deviceResult.resize(1);

    flux::launchLinearKernel(
        1,
        ReconstructTriangleInteraction{
            .geometry =
                {
                    .vertices = deviceVertices.data(),
                    .triangles = deviceTriangles.data(),
                    .numVertices = static_cast<std::uint32_t>(vertices.size()),
                    .numTriangles = static_cast<std::uint32_t>(triangles.size()),
                },
            .preliminary =
                {
                    .distance = 3.0F,
                    .coordinates = {0.25F, 0.5F},
                    .elementIndex = 0,
                },
            .result = deviceResult.data(),
        },
        cudaStreamPerThread
    );

    std::array<flux::GeometryInteraction, 1> result{};
    deviceResult.copyToHost(result);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    EXPECT_EQ(result[0].position, (flux::Vec3f{0.5F, 2.0F, 0.0F}));
    EXPECT_EQ(result[0].geometricNormal, (flux::Vec3f{0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(result[0].shadingNormal, result[0].geometricNormal);
    EXPECT_EQ(result[0].uv, (flux::Vec2f{}));
    EXPECT_EQ(result[0].elementIndex, 0);
}

TEST(GeometryTests, InterpolatesIndexedShadingAttributes) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    std::array const vertices{
        flux::Vec3f{0.0F, 0.0F, 0.0F},
        flux::Vec3f{2.0F, 0.0F, 0.0F},
        flux::Vec3f{0.0F, 4.0F, 0.0F},
    };
    std::array const triangles{flux::Vec3u{0, 1, 2}};
    std::array const normals{
        flux::Vec3f{1.0F, 0.0F, 0.0F},
        flux::Vec3f{0.0F, 1.0F, 0.0F},
        flux::Vec3f{0.0F, 0.0F, 1.0F},
    };
    std::array const normalIndices{flux::Vec3u{1, 2, 0}};
    std::array const texCoords{
        flux::Vec2f{0.1F, 0.2F},
        flux::Vec2f{0.3F, 0.4F},
        flux::Vec2f{0.5F, 0.6F},
    };
    std::array const texCoordIndices{flux::Vec3u{2, 0, 1}};

    flux::DeviceBuffer<flux::Vec3f> deviceVertices(cudaStreamPerThread);
    flux::DeviceBuffer<flux::Vec3u> deviceTriangles(cudaStreamPerThread);
    flux::DeviceBuffer<flux::Vec3f> deviceNormals(cudaStreamPerThread);
    flux::DeviceBuffer<flux::Vec3u> deviceNormalIndices(cudaStreamPerThread);
    flux::DeviceBuffer<flux::Vec2f> deviceTexCoords(cudaStreamPerThread);
    flux::DeviceBuffer<flux::Vec3u> deviceTexCoordIndices(cudaStreamPerThread);
    flux::DeviceBuffer<flux::GeometryInteraction> deviceResult(cudaStreamPerThread);
    deviceVertices.copyFromHost(vertices);
    deviceTriangles.copyFromHost(triangles);
    deviceNormals.copyFromHost(normals);
    deviceNormalIndices.copyFromHost(normalIndices);
    deviceTexCoords.copyFromHost(texCoords);
    deviceTexCoordIndices.copyFromHost(texCoordIndices);
    deviceResult.resize(1);

    flux::launchLinearKernel(
        1,
        ReconstructTriangleInteraction{
            .geometry =
                {
                    .vertices = deviceVertices.data(),
                    .triangles = deviceTriangles.data(),
                    .normals = deviceNormals.data(),
                    .normalIndices = deviceNormalIndices.data(),
                    .texCoords = deviceTexCoords.data(),
                    .texCoordIndices = deviceTexCoordIndices.data(),
                    .numVertices = static_cast<std::uint32_t>(vertices.size()),
                    .numTriangles = static_cast<std::uint32_t>(triangles.size()),
                },
            .preliminary =
                {
                    .distance = 3.0F,
                    .coordinates = {0.25F, 0.5F},
                    .elementIndex = 0,
                },
            .result = deviceResult.data(),
        },
        cudaStreamPerThread
    );

    std::array<flux::GeometryInteraction, 1> result{};
    deviceResult.copyToHost(result);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    EXPECT_EQ(result[0].geometricNormal, (flux::Vec3f{0.0F, 0.0F, 1.0F}));
    EXPECT_NEAR(result[0].shadingNormal.x(), 0.8164966F, 1.0e-6F);
    EXPECT_NEAR(result[0].shadingNormal.y(), 0.4082483F, 1.0e-6F);
    EXPECT_NEAR(result[0].shadingNormal.z(), 0.4082483F, 1.0e-6F);
    EXPECT_NEAR(result[0].uv.x(), 0.3F, 1.0e-6F);
    EXPECT_NEAR(result[0].uv.y(), 0.4F, 1.0e-6F);
}
