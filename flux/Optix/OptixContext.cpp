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
#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixSbt.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "kira/Anyhow.h"
#include "kira/Assertions.h"

namespace flux {
struct OptixContext::Impl : private CudaStreamMixin {
    Impl(
        Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
        std::filesystem::path modulePath
    )
        : CudaStreamMixin(stream), context(context), deviceContext(deviceContext),
          modulePath(std::move(modulePath)), geometryPool(stream), accel(stream), sbt(stream),
          primitives(stream), bsdfs(stream) {}

    void sync() try {
        context.commit();

        // Rebuild the pipeline first because the SBT packs its program-group headers.
        auto const spec = OptixProgram::makeSpec(context);
        program.reset();
        program = std::make_unique<OptixProgram>(deviceContext, modulePath, spec);

        auto const contextPrimitives = context.getObjects<Primitive>();
        auto const contextBSDFs = context.getObjects<BSDF>();
        kira::SmallVector<Ref<TriangleMesh const>> uniqueMeshes;
        std::unordered_map<std::size_t, std::uint32_t> geometryIndexByContextId;
        std::unordered_map<std::size_t, std::uint32_t> bsdfIndexByContextId;
        std::vector<OptixAccel::InstanceDesc> instanceDescs;
        primitiveStaging.clear();
        bsdfStaging.clear();
        uniqueMeshes.reserve(contextPrimitives.size());
        geometryIndexByContextId.reserve(contextPrimitives.size());
        bsdfIndexByContextId.reserve(contextBSDFs.size());
        instanceDescs.reserve(contextPrimitives.size());
        primitiveStaging.reserve(contextPrimitives.size());
        bsdfStaging.reserve(contextBSDFs.size());

        if (contextBSDFs.size() > Primitive::Impl::invalidBSDFIndex)
            throw kira::Anyhow("OptixContext: BSDF count exceeds device limits");

        // Context IDs may contain gaps. Assign each BSDF a dense OptiX scene index.
        for (auto const &bsdf : contextBSDFs) {
            auto const index = static_cast<std::uint32_t>(bsdfStaging.size());
            bsdfIndexByContextId.emplace(bsdf->getContextId(), index);
            bsdfStaging.push_back(bsdf->getImpl());
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
            primitiveStaging.push_back({
                .geometryIndex = geometryIndex,
                .bsdfIndex = bsdfIndex,
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
        auto const buildInputs = geometryPool.getBuildInputs();
        accel.buildGas(deviceContext, buildInputs);
        primitives.copyFromHost({primitiveStaging.data(), primitiveStaging.size()});
        bsdfs.copyFromHost({bsdfStaging.data(), bsdfStaging.size()});
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
    OptixAccel accel;
    OptixSbt sbt;
    std::vector<Primitive::Impl> primitiveStaging;
    DeviceBuffer<Primitive::Impl> primitives;
    std::vector<BSDF::Impl> bsdfStaging;
    DeviceBuffer<BSDF::Impl> bsdfs;
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
        .bsdfs = impl_->bsdfs.data(),
        .numGeometries = static_cast<std::uint32_t>(impl_->geometryPool.size()),
        .numPrimitives = static_cast<std::uint32_t>(impl_->primitives.size()),
        .numBSDFs = static_cast<std::uint32_t>(impl_->bsdfs.size()),
    };
}
} // namespace flux
