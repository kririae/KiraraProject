#include "flux/Optix/OptixContext.h"

#include <optix_stubs.h>

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixAccel.h"
#include "flux/Optix/OptixGeometryPool.h"
#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixSbt.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "kira/Anyhow.h"

namespace flux {
struct OptixContext::Impl : private CudaStreamMixin {
    Impl(
        Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
        std::filesystem::path const &modulePath
    )
        : CudaStreamMixin(stream), context(context), deviceContext(deviceContext),
          program(deviceContext, modulePath), geometryPool(stream), accel(stream), sbt(stream),
          primitives(stream) {}

    void sync() {
        context.commit();

        try {
            auto const scenePrimitives = context.getObjects<Primitive>();
            kira::SmallVector<Ref<TriangleMesh const>> meshes;
            std::unordered_map<std::size_t, std::uint32_t> geometryIndices;
            std::vector<OptixAccel::InstanceDesc> instances;
            primitiveStaging.clear();
            meshes.reserve(scenePrimitives.size());
            geometryIndices.reserve(scenePrimitives.size());
            instances.reserve(scenePrimitives.size());
            primitiveStaging.reserve(scenePrimitives.size());

            for (auto const &primitive : scenePrimitives) {
                if (!primitive->isVisible())
                    continue;

                auto const contextId = primitive->getGeometryContextId();
                auto iterator = geometryIndices.find(contextId);
                if (iterator == geometryIndices.end()) {
                    if (meshes.size() >= std::numeric_limits<std::uint32_t>::max())
                        throw kira::Anyhow("OptixContext: geometry count exceeds device limits");
                    auto const index = static_cast<std::uint32_t>(meshes.size());
                    meshes.push_back(primitive->getGeometry());
                    iterator = geometryIndices.emplace(contextId, index).first;
                }

                primitiveStaging.push_back({.geometryIndex = iterator->second});
                instances.push_back({
                    .geometryIndex = iterator->second,
                    .transform = primitive->getTransform(),
                });
            }

            geometryPool.build(meshes);
            auto const buildInputs = geometryPool.getBuildInputs();
            accel.buildGas(deviceContext, buildInputs);
            primitives.copyFromHost({primitiveStaging.data(), primitiveStaging.size()});
            accel.buildIas(deviceContext, instances);
            sbt.build(program);
            cudaCheck(cudaStreamSynchronize(getStream()));
        } catch (...) {
            cudaCheck<false>(cudaStreamSynchronize(getStream()));
            throw;
        }
    }

    Context &context;
    OptixDeviceContext deviceContext;
    OptixProgram program;
    OptixGeometryPool geometryPool;
    OptixAccel accel;
    OptixSbt sbt;
    std::vector<Primitive::DeviceImpl> primitiveStaging;
    DeviceBuffer<Primitive::DeviceImpl> primitives;
};

OptixContext::OptixContext(
    Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
    std::filesystem::path const &modulePath
)
    : impl_(std::make_unique<Impl>(context, deviceContext, stream, modulePath)) {}

OptixContext::~OptixContext() = default;

void OptixContext::sync() { impl_->sync(); }

void OptixContext::launch(
    cudaStream_t stream, CUdeviceptr params, std::size_t paramsSize, std::uint32_t width,
    std::uint32_t height
) const {
    // clang-format off
    optixCheck(optixLaunch(
        /* pipeline =           */ impl_->program.getPipeline(),
        /* stream =             */ stream,
        /* pipelineParams =     */ params,
        /* pipelineParamsSize = */ paramsSize,
        /* sbt =                */ &impl_->sbt.getTable(),
        /* width =              */ width,
        /* height =             */ height,
        /* depth =              */ 1));
    // clang-format on
}

OptixContext::DeviceImpl OptixContext::getDeviceImpl() const noexcept {
    return {
        .traversable = impl_->accel.getHandle(),
        .geometries = impl_->geometryPool.getDeviceImpls(),
        .primitives = impl_->primitives.data(),
        .numGeometries = static_cast<std::uint32_t>(impl_->geometryPool.size()),
        .numPrimitives = static_cast<std::uint32_t>(impl_->primitives.size()),
    };
}
} // namespace flux
