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
#include "flux/Sampling/Sampler.h"
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
          modulePath(modulePath), geometryPool(stream), accel(stream), sbt(stream),
          primitives(stream) {}

    void sync() try {
        context.commit();

        // Rebuild the pipeline first because the SBT packs its program-group headers.
        auto const spec = OptixProgram::makeSpec(context);
        program.reset();
        program = std::make_unique<OptixProgram>(deviceContext, modulePath, spec);

        auto const scenePrimitives = context.getObjects<Primitive>();
        kira::SmallVector<Ref<TriangleMesh const>> uniqueMeshes;
        std::unordered_map<std::size_t, std::uint32_t> geometryIndexByContextId;
        std::vector<OptixAccel::InstanceDesc> instanceDescs;
        primitiveStaging.clear();
        uniqueMeshes.reserve(scenePrimitives.size());
        geometryIndexByContextId.reserve(scenePrimitives.size());
        instanceDescs.reserve(scenePrimitives.size());
        primitiveStaging.reserve(scenePrimitives.size());

        // Flatten the visible scene into the dense arrays used on the device.
        // Several primitives may share one geometry, so assign each mesh one
        // backend-local index before building the primitive and instance arrays.
        auto const getOrAddGeometryIndex = [&](Primitive const &primitive) {
            auto const contextId = primitive.getGeometryContextId();
            if (auto const iterator = geometryIndexByContextId.find(contextId);
                iterator != geometryIndexByContextId.end())
                return iterator->second;

            if (uniqueMeshes.size() >= std::numeric_limits<std::uint32_t>::max())
                throw kira::Anyhow("OptixContext: geometry count exceeds device limits");

            auto const index = static_cast<std::uint32_t>(uniqueMeshes.size());
            uniqueMeshes.push_back(primitive.getGeometry());
            geometryIndexByContextId.emplace(contextId, index);
            return index;
        };

        for (auto const &primitive : scenePrimitives) {
            if (!primitive->isVisible())
                continue;

            auto const geometryIndex = getOrAddGeometryIndex(*primitive);
            primitiveStaging.push_back({.geometryIndex = geometryIndex});
            instanceDescs.push_back({
                .geometryIndex = geometryIndex,
                .transform = primitive->getTransform(),
            });
        }

        // Rebuild in dependency order. GAS consumes the geometry buffers; IAS
        // then consumes the GAS handles and the matching primitive layout.
        geometryPool.build(uniqueMeshes);
        auto const buildInputs = geometryPool.getBuildInputs();
        accel.buildGas(deviceContext, buildInputs);
        primitives.copyFromHost({primitiveStaging.data(), primitiveStaging.size()});
        accel.buildIas(deviceContext, instanceDescs);
        sbt.build(*program);

        // Publish the new snapshot only after every queued upload and build has finished.
        cudaCheck(cudaStreamSynchronize(getStream()));
    } catch (...) {
        cudaCheck<false>(cudaStreamSynchronize(getStream()));
        throw;
    }

    Context &context;
    OptixDeviceContext deviceContext;
    std::filesystem::path modulePath;
    std::unique_ptr<OptixProgram> program;
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
    cudaStream_t stream, CUdeviceptr params, std::size_t paramsSize, std::uint32_t size
) const {
    // clang-format off
    optixCheck(optixLaunch(
        /* pipeline =           */ impl_->program->getPipeline(),
        /* stream =             */ stream,
        /* pipelineParams =     */ params,
        /* pipelineParamsSize = */ paramsSize,
        /* sbt =                */ &impl_->sbt.getTable(),
        /* width =              */ size,
        /* height =             */ 1,
        /* depth =              */ 1));
    // clang-format on
}

OptixProgramSpec const &OptixContext::getProgramSpec() const noexcept {
    return impl_->program->getSpec();
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
