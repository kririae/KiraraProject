#include "flux/Optix/OptixContext.h"

#include <optix_stubs.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "flux/Core/Logging.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixAccel.h"
#include "flux/Optix/OptixGeometryPool.h"
#include "flux/Optix/OptixImageTexturePool.h"
#include "flux/Optix/OptixLightSampler.h"
#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixSbt.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/SceneTableData.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

namespace flux {
struct OptixContext::Storage : private CudaStreamMixin {
    Storage(
        Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
        std::filesystem::path modulePath
    )
        : CudaStreamMixin(stream), context(context), deviceContext(deviceContext),
          modulePath(std::move(modulePath)) {}

    void sync() try {
        context.commit();

        // Rebuild the pipeline first because the SBT packs its program-group headers.
        auto const spec = OptixProgram::makeSpec(context);
        program.reset();
        program = std::make_unique<OptixProgram>(deviceContext, modulePath, spec);

        table.build(context);

        // An OptiX instance ID is a dense primitive index.
        std::vector<OptixAccel::InstanceDesc> instanceDescs;
        instanceDescs.reserve(table.primitives.size());
        for (std::size_t index = 0; index < table.primitives.size(); ++index) {
            auto const &primitive = table.primitives[index];
            auto const geometryIndex = primitive.getGeometryIndex();
            auto const bsdfType = primitive.hasBSDF() ? table.bsdfs[primitive.getBSDFIndex()].type
                                                      : BSDFType::Diffuse;
            instanceDescs.push_back({
                .geometryIndex = geometryIndex,
                .sbtOffset = OptixSbt::getInstanceOffset(
                    bsdfType, table.objects.meshes[geometryIndex]->getType()
                ),
                .transform = table.transforms[index],
            });
        }

        // Rebuild in dependency order. GAS consumes the geometry buffers; IAS
        // then consumes the GAS handles and the matching primitive layout.
        geometryPool.build(table.objects.meshes);
        imageTexturePool.build(context);
        auto const buildInputs = geometryPool.getBuildInputs();
        accel.buildGas(deviceContext, buildInputs);
        accel.buildIas(deviceContext, instanceDescs);
        lightSampler.build(table, context, imageTexturePool.getImpl(), accel.getSceneRadius());
        primitives.copyFromHost({table.primitives.data(), table.primitives.size()});
        bsdfs.copyFromHost({table.bsdfs.data(), table.bsdfs.size()});
        edfs.copyFromHost({table.edfs.data(), table.edfs.size()});
        sbt.build(*program);

        // Wait for all queued uploads and builds before returning.
        cudaCheck(cudaStreamSynchronize(getStream()));
        geometryPool.releaseHostStaging();
        LogDebug(
            "OptixContext: built {} geometries and {} visible primitives",
            std::ranges::count_if(
                table.objects.meshes, [](auto const &mesh) { return static_cast<bool>(mesh); }
            ),
            table.primitives.size()
        );
    } catch (...) {
        cudaCheck<false>(cudaStreamSynchronize(getStream()));
        throw;
    }

    Context &context;
    OptixDeviceContext deviceContext;
    std::filesystem::path modulePath;
    std::unique_ptr<OptixProgram> program;
    SceneTableData table;
    OptixGeometryPool geometryPool{getStream()};
    OptixImageTexturePool imageTexturePool{getStream()};
    OptixLightSampler lightSampler{getStream()};
    OptixAccel accel{getStream()};
    OptixSbt sbt{getStream()};
    DeviceBuffer<Primitive::Impl> primitives{getStream()};
    DeviceBuffer<BSDF::Impl> bsdfs{getStream()};
    DeviceBuffer<EDF::Impl> edfs{getStream()};
};

OptixContext::OptixContext(
    Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
    std::filesystem::path const &modulePath
)
    : storage_(std::make_unique<Storage>(context, deviceContext, stream, modulePath)) {}

OptixContext::~OptixContext() = default;

void OptixContext::sync() { storage_->sync(); }

void OptixContext::launch(
    cudaStream_t stream, CUdeviceptr params, std::size_t paramsSize, std::uint32_t size
) const {
    // clang-format off
    optixCheck(optixLaunch(
        /* pipeline =           */ storage_->program->getPipeline(),
        /* stream =             */ stream,
        /* pipelineParams =     */ params,
        /* pipelineParamsSize = */ paramsSize,
        /* sbt =                */ &storage_->sbt.getTable(),
        /* width =              */ size,
        /* height =             */ 1,
        /* depth =              */ 1));
    // clang-format on
}

OptixProgramSpec const &OptixContext::getProgramSpec() const noexcept {
    return storage_->program->getSpec();
}

OptixContext::Impl OptixContext::getImpl() const noexcept {
    return {
        .traversable = storage_->accel.getHandle(),
        .table =
            {
                .geometries = storage_->geometryPool.getDeviceImpls(),
                .primitives = storage_->primitives.data(),
                .bsdfs = storage_->bsdfs.data(),
                .edfs = storage_->edfs.data(),
            },
        .imageTexturePool = storage_->imageTexturePool.getImpl(),
        .lightSampler = storage_->lightSampler.getImpl(),
    };
}
} // namespace flux
