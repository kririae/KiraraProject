#include "flux/Optix/OptixContext.h"

#include <optix_stubs.h>

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

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
          modulePath(std::move(modulePath)), geometryPool(stream), imageTexturePool(stream),
          lightSampler(stream), accel(stream), sbt(stream), primitives(stream), bsdfs(stream),
          edfs(stream) {}

    void sync() try {
        context.commit();

        // Rebuild the pipeline first because the SBT packs its program-group headers.
        auto const spec = OptixProgram::makeSpec(context);
        program.reset();
        program = std::make_unique<OptixProgram>(deviceContext, modulePath, spec);

        auto const contextPrimitives = context.getObjects<Primitive>();
        auto const contextImageTextures = context.getObjects<ImageTexture>();
        auto const contextBSDFs = context.getObjects<BSDF>();
        auto const contextEDFs = context.getObjects<EDF>();
        auto const contextLights = context.getObjects<Light>();
        kira::SmallVector<Ref<TriangleMesh const>> uniqueMeshes;
        kira::SmallVector<Ref<Primitive const>> visiblePrimitives;
        std::unordered_map<std::size_t, std::uint32_t> geometryIndexByContextId;
        std::unordered_map<std::size_t, std::uint32_t> bsdfIndexByContextId;
        std::unordered_map<std::size_t, std::uint32_t> edfIndexByContextId;
        std::vector<OptixAccel::InstanceDesc> instanceDescs;
        primitiveStaging.clear();
        bsdfStaging.clear();
        edfStaging.clear();
        uniqueMeshes.reserve(contextPrimitives.size());
        visiblePrimitives.reserve(contextPrimitives.size());
        geometryIndexByContextId.reserve(contextPrimitives.size());
        bsdfIndexByContextId.reserve(contextBSDFs.size());
        edfIndexByContextId.reserve(contextEDFs.size());
        instanceDescs.reserve(contextPrimitives.size());
        primitiveStaging.reserve(contextPrimitives.size());
        bsdfStaging.reserve(contextBSDFs.size());
        edfStaging.reserve(contextEDFs.size());

        if (contextBSDFs.size() > Primitive::Impl::invalidBSDFIndex)
            throw kira::Anyhow("OptixContext: BSDF count exceeds device limits");
        if (contextEDFs.size() > Primitive::Impl::invalidEDFIndex)
            throw kira::Anyhow("OptixContext: EDF count exceeds device limits");

        // Context IDs may contain gaps. Assign each BSDF a dense OptiX scene index.
        for (auto const &bsdf : contextBSDFs) {
            auto const index = static_cast<std::uint32_t>(bsdfStaging.size());
            bsdfIndexByContextId.emplace(bsdf->getContextId(), index);
            bsdfStaging.push_back(bsdf->getImpl());
        }
        for (auto const &edf : contextEDFs) {
            auto const index = static_cast<std::uint32_t>(edfStaging.size());
            edfIndexByContextId.emplace(edf->getContextId(), index);
            edfStaging.push_back(edf->getImpl());
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
            if (bsdf) {
                auto const iterator = bsdfIndexByContextId.find(bsdf->getContextId());
                KIRA_ASSERT(
                    iterator != bsdfIndexByContextId.end(),
                    "Linked BSDF is missing from the OptiX scene"
                );
                bsdfIndex = iterator->second;
            }
            auto const edf = primitive->getEDF();
            auto edfIndex = Primitive::Impl::invalidEDFIndex;
            if (edf) {
                auto const iterator = edfIndexByContextId.find(edf->getContextId());
                KIRA_ASSERT(
                    iterator != edfIndexByContextId.end(),
                    "Linked EDF is missing from the OptiX scene"
                );
                edfIndex = iterator->second;
            }
            primitiveStaging.push_back({
                .geometryIndex = geometryIndex,
                .bsdfIndex = bsdfIndex,
                .edfIndex = edfIndex,
            });
            visiblePrimitives.push_back(primitive);
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
        imageTexturePool.build(contextImageTextures);
        lightSampler.build(contextLights, visiblePrimitives, primitiveStaging);
        auto const buildInputs = geometryPool.getBuildInputs();
        accel.buildGas(deviceContext, buildInputs);
        primitives.copyFromHost({primitiveStaging.data(), primitiveStaging.size()});
        bsdfs.copyFromHost({bsdfStaging.data(), bsdfStaging.size()});
        edfs.copyFromHost({edfStaging.data(), edfStaging.size()});
        accel.buildIas(deviceContext, instanceDescs);
        sbt.build(*program);

        // Wait for all queued uploads and builds before returning.
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
    OptixImageTexturePool imageTexturePool;
    OptixLightSampler lightSampler;
    OptixAccel accel;
    OptixSbt sbt;
    std::vector<Primitive::Impl> primitiveStaging;
    DeviceBuffer<Primitive::Impl> primitives;
    std::vector<BSDF::Impl> bsdfStaging;
    DeviceBuffer<BSDF::Impl> bsdfs;
    std::vector<EDF::Impl> edfStaging;
    DeviceBuffer<EDF::Impl> edfs;
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
        .lightSampler = storage_->lightSampler.getSampler(),
        .numGeometries = static_cast<std::uint32_t>(storage_->geometryPool.size()),
        .numPrimitives = static_cast<std::uint32_t>(storage_->primitives.size()),
        .numBSDFs = static_cast<std::uint32_t>(storage_->bsdfs.size()),
        .numEDFs = static_cast<std::uint32_t>(storage_->edfs.size()),
    };
}
} // namespace flux
