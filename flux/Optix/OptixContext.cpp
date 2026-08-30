#include "flux/Optix/OptixContext.h"

#include <optix_stubs.h>

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
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
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"
#include "kira/Assertions.h"

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

        auto const contextPrimitives = context.getObjects<Primitive>();
        auto const contextBSDFs = context.getObjects<BSDF>();
        auto const contextEDFs = context.getObjects<EDF>();
        kira::SmallVector<Ref<TriangleMesh const>> uniqueMeshes;
        std::unordered_map<std::size_t, std::uint32_t> geometryIndexByContextId;
        std::vector<OptixAccel::InstanceDesc> instanceDescs;
        primitiveStaging.clear();
        bsdfStaging.clear();
        edfStaging.clear();
        uniqueMeshes.reserve(contextPrimitives.size());
        geometryIndexByContextId.reserve(contextPrimitives.size());
        instanceDescs.reserve(contextPrimitives.size());
        primitiveStaging.reserve(contextPrimitives.size());

        // Use a live implementation for unused indices. Primitive indices
        // select the live entries filled below.
        if (!contextBSDFs.empty()) {
            bsdfStaging.assign(context.getBSDFIndexLimit(), contextBSDFs.front()->getImpl());
            for (auto const &bsdf : contextBSDFs)
                bsdfStaging[context.getBSDFIndex(bsdf->getContextId())] = bsdf->getImpl();
        }
        if (!contextEDFs.empty()) {
            edfStaging.assign(context.getEDFIndexLimit(), contextEDFs.front()->getImpl());
            for (auto const &edf : contextEDFs)
                edfStaging[context.getEDFIndex(edf->getContextId())] = edf->getImpl();
        }

        // Pack visible primitives into dense OptiX arrays. Shared meshes use one
        // OptiX scene geometry index.
        auto const getOrAddGeometryIndex = [&](Ref<Geometry const> const &geometry) {
            auto const contextId = geometry->getContextId();
            if (auto const iterator = geometryIndexByContextId.find(contextId);
                iterator != geometryIndexByContextId.end())
                return iterator->second;

            if (uniqueMeshes.size() >= std::numeric_limits<std::uint32_t>::max())
                throw kira::Anyhow("OptixContext: geometry count exceeds device limits");

            auto const index = static_cast<std::uint32_t>(uniqueMeshes.size());
            switch (geometry->getType()) {
            case GeometryType::TriangleMesh: {
                auto mesh = geometry.dynamicCast<TriangleMesh const>();
                if (!mesh)
                    throw kira::Anyhow(
                        "OptixContext: geometry type does not match its host implementation"
                    );
                uniqueMeshes.push_back(std::move(mesh));
                break;
            }
            case GeometryType::Count: throw kira::Anyhow("OptixContext: unsupported geometry type");
            }
            geometryIndexByContextId.emplace(contextId, index);
            return index;
        };

        for (auto const &primitive : contextPrimitives) {
            if (!primitive->isVisible())
                continue;
            if (primitiveStaging.size() >= std::numeric_limits<std::uint32_t>::max())
                throw kira::Anyhow("OptixContext: primitive count exceeds device limits");

            auto const geometry = primitive->getGeometry();
            auto const geometryIndex = getOrAddGeometryIndex(geometry);
            auto const bsdf = primitive->getBSDF();
            auto bsdfIndex = Primitive::Impl::invalidBSDFIndex;
            if (bsdf)
                bsdfIndex = context.getBSDFIndex(bsdf->getContextId());
            auto const edf = primitive->getEDF();
            auto edfIndex = Primitive::Impl::invalidEDFIndex;
            if (edf)
                edfIndex = context.getEDFIndex(edf->getContextId());
            primitiveStaging.push_back({
                .geometryIndex = geometryIndex,
                .bsdfIndex = bsdfIndex,
                .edfIndex = edfIndex,
            });
            auto const bsdfType = bsdf ? bsdf->getType() : BSDFType::Diffuse;
            instanceDescs.push_back({
                .geometryIndex = geometryIndex,
                .sbtOffset = OptixSbt::getInstanceOffset(bsdfType, geometry->getType()),
                .transform = primitive->getTransform(),
            });
        }

        // Rebuild in dependency order. GAS consumes the geometry buffers; IAS
        // then consumes the GAS handles and the matching primitive layout.
        geometryPool.build(uniqueMeshes);
        imageTexturePool.build(context);
        auto const buildInputs = geometryPool.getBuildInputs();
        accel.buildGas(deviceContext, buildInputs);
        accel.buildIas(deviceContext, instanceDescs);
        lightSampler.build(
            context, primitiveStaging, imageTexturePool.getImpl(), accel.getSceneRadius()
        );
        primitives.copyFromHost({primitiveStaging.data(), primitiveStaging.size()});
        bsdfs.copyFromHost({bsdfStaging.data(), bsdfStaging.size()});
        edfs.copyFromHost({edfStaging.data(), edfStaging.size()});
        sbt.build(*program);

        // Wait for all queued uploads and builds before returning.
        cudaCheck(cudaStreamSynchronize(getStream()));
        LogDebug(
            "OptixContext: built {} geometries and {} visible primitives", uniqueMeshes.size(),
            primitiveStaging.size()
        );
    } catch (...) {
        cudaCheck<false>(cudaStreamSynchronize(getStream()));
        throw;
    }

    Context &context;
    OptixDeviceContext deviceContext;
    std::filesystem::path modulePath;
    std::unique_ptr<OptixProgram> program;
    OptixGeometryPool geometryPool{getStream()};
    OptixImageTexturePool imageTexturePool{getStream()};
    OptixLightSampler lightSampler{getStream()};
    OptixAccel accel{getStream()};
    OptixSbt sbt{getStream()};
    std::vector<Primitive::Impl> primitiveStaging;
    DeviceBuffer<Primitive::Impl> primitives{getStream()};
    std::vector<BSDF::Impl> bsdfStaging;
    DeviceBuffer<BSDF::Impl> bsdfs{getStream()};
    std::vector<EDF::Impl> edfStaging;
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
        .geometries = storage_->geometryPool.getDeviceImpls(),
        .primitives = storage_->primitives.data(),
        .bsdfs = storage_->bsdfs.data(),
        .edfs = storage_->edfs.data(),
        .imageTexturePool = storage_->imageTexturePool.getImpl(),
        .lightSampler = storage_->lightSampler.getImpl(),
        .numGeometries = static_cast<std::uint32_t>(storage_->geometryPool.size()),
        .numPrimitives = static_cast<std::uint32_t>(storage_->primitives.size()),
        .bsdfIndexLimit = static_cast<std::uint32_t>(storage_->bsdfs.size()),
        .edfIndexLimit = static_cast<std::uint32_t>(storage_->edfs.size()),
    };
}
} // namespace flux
