#include "flux/Optix/OptixContext.h"

#include <optix_stubs.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "flux/Core/EnumFlags.h"
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
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/SceneTableData.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Assertions.h"

namespace flux {
namespace {
/// What a sync rebuilds. For now it is a choice between all of the device state and none of it.
enum class Plan : std::uint8_t {
    Nothing,
    Everything,
};

/// Returns the plan for one change recorded in the context.
///
/// Each overload below consumes the enumerators of one \c DirtyBits type with no \c default, so
/// that adding an enumerator makes the compiler report the \c switch until it is handled. A change
/// that needs no device work gets an explicit case that returns \c Plan::Nothing.
[[nodiscard]] Plan planBit(Context::DirtyBits bit) {
    switch (bit) {
    case Context::DirtyBits::None: return Plan::Nothing;
    case Context::DirtyBits::ActiveIntegrator: return Plan::Everything;
    case Context::DirtyBits::ActiveSampler: return Plan::Everything;
    case Context::DirtyBits::ActiveEnvMap: return Plan::Everything;
    }

    // Not an enumerator, so the safe answer is the full rebuild.
    return Plan::Everything;
}

[[nodiscard]] Plan planBit(Primitive::DirtyBits bit) {
    switch (bit) {
    case Primitive::DirtyBits::None: return Plan::Nothing;
    case Primitive::DirtyBits::Geometry: return Plan::Everything;
    case Primitive::DirtyBits::BSDF: return Plan::Everything;
    case Primitive::DirtyBits::EDF: return Plan::Everything;
    case Primitive::DirtyBits::Transform: return Plan::Everything;
    case Primitive::DirtyBits::Visibility: return Plan::Everything;
    }
    return Plan::Everything;
}

[[nodiscard]] Plan planBit(PointLight::DirtyBits bit) {
    switch (bit) {
    case PointLight::DirtyBits::None: return Plan::Nothing;
    case PointLight::DirtyBits::Position: return Plan::Everything;
    case PointLight::DirtyBits::Intensity: return Plan::Everything;
    }
    return Plan::Everything;
}

[[nodiscard]] Plan planBit(EnvMapLight::DirtyBits bit) {
    switch (bit) {
    case EnvMapLight::DirtyBits::None: return Plan::Nothing;
    case EnvMapLight::DirtyBits::Texture: return Plan::Everything;
    case EnvMapLight::DirtyBits::Scale: return Plan::Everything;
    case EnvMapLight::DirtyBits::Rotation: return Plan::Everything;
    }
    return Plan::Everything;
}

[[nodiscard]] Plan planBit(ConstantEDF::DirtyBits bit) {
    switch (bit) {
    case ConstantEDF::DirtyBits::None: return Plan::Nothing;
    case ConstantEDF::DirtyBits::Radiance: return Plan::Everything;
    }
    return Plan::Everything;
}

/// Returns the largest plan among the set bits of \p bits.
template <typename Bits> [[nodiscard]] Plan planBits(Bits bits) {
    auto plan = Plan::Nothing;
    forEachBit(bits, [&](Bits bit) { plan = std::max(plan, planBit(bit)); });
    return plan;
}

/// Returns the plan for the bits that \p object recorded.
///
/// Only the types below have setters, so a changed object of another type cannot exist.
[[nodiscard]] Plan planChanged(ContextObject const &object) {
    if (auto const *primitive = dynamic_cast<Primitive const *>(&object))
        return planBits(getDirtyBits(*primitive));
    if (auto const *point = dynamic_cast<PointLight const *>(&object))
        return planBits(getDirtyBits(*point));
    if (auto const *envMap = dynamic_cast<EnvMapLight const *>(&object))
        return planBits(getDirtyBits(*envMap));
    if (auto const *edf = dynamic_cast<ConstantEDF const *>(&object))
        return planBits(getDirtyBits(*edf));

    KIRA_ASSERT(false, "OptixContext: a changed object has a type without dirty bits");
    return Plan::Everything;
}
} // namespace

struct OptixContext::pImpl final : private CudaStreamMixin {
    pImpl(
        Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
        std::filesystem::path modulePath
    )
        : CudaStreamMixin(stream), context(context), deviceContext(deviceContext),
          modulePath(std::move(modulePath)) {}

    /// Plans what to rebuild, then rebuilds it.
    void sync();

    /// Reads the context's records of this epoch and decides what to rebuild.
    [[nodiscard]] Plan plan() const;

    /// Rebuilds all of the device state from the context.
    void rebuild();

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
    DeviceBuffer<std::uint32_t> instanceIndices{getStream()};
    DeviceBuffer<BSDF::Impl> bsdfs{getStream()};
    DeviceBuffer<EDF::Impl> edfs{getStream()};

    /// Epoch of the last successful sync, or nothing before the first sync and after a failed
    /// one, so the next sync rebuilds everything.
    std::optional<std::uint64_t> syncedEpoch;
};

OptixContext::OptixContext(
    Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
    std::filesystem::path const &modulePath
)
    : pImpl_(std::make_unique<pImpl>(context, deviceContext, stream, modulePath)) {}

OptixContext::~OptixContext() = default;

void OptixContext::sync() { pImpl_->sync(); }

void OptixContext::launch(
    cudaStream_t stream, CUdeviceptr params, std::size_t paramsSize, std::uint32_t size
) const {
    // clang-format off
    optixCheck(optixLaunch(
        /* pipeline =           */ pImpl_->program->getPipeline(),
        /* stream =             */ stream,
        /* pipelineParams =     */ params,
        /* pipelineParamsSize = */ paramsSize,
        /* sbt =                */ &pImpl_->sbt.getTable(),
        /* width =              */ size,
        /* height =             */ 1,
        /* depth =              */ 1));
    // clang-format on
}

OptixProgramSpec const &OptixContext::getProgramSpec() const noexcept {
    return pImpl_->program->getSpec();
}

OptixContext::Impl OptixContext::getImpl() const noexcept {
    return {
        .traversable = pImpl_->accel.getHandle(),
        .instanceIndices = pImpl_->instanceIndices.data(),
        .table =
            {
                .geometries = pImpl_->geometryPool.getDeviceImpls(),
                .primitives = pImpl_->primitives.data(),
                .bsdfs = pImpl_->bsdfs.data(),
                .edfs = pImpl_->edfs.data(),
            },
        .imageTexturePool = pImpl_->imageTexturePool.getImpl(),
        .lightSampler = pImpl_->lightSampler.getImpl(),
    };
}

void OptixContext::pImpl::sync() try {
    // Plan.
    if (plan() == Plan::Everything)
        rebuild();

    // Record the epoch only now, after the work succeeded.
    syncedEpoch = context.getEpoch();
} catch (...) {
    // Forget the last sync, because the failure may have left part of its state replaced.
    syncedEpoch.reset();
    cudaCheck<false>(cudaStreamSynchronize(getStream()));
    throw;
}

Plan OptixContext::pImpl::plan() const {
    // Missed epochs. The context keeps the records of the current epoch only, and each
    // \c clearDirty drops them. A sync in epoch S has read every edit made until then, because the
    // host syncs before it clears. So when the context is still in S, or one clear past it, the
    // records of the current epoch describe every edit since the last sync. After more than one
    // clear, the records of the epochs in between are gone and nothing says what changed, so only
    // a full rebuild is correct. A runtime that has not synced has no state to patch either.
    auto const epoch = context.getEpoch();
    if (!syncedEpoch || (epoch != *syncedEpoch && epoch != *syncedEpoch + 1))
        return Plan::Everything;

    // Context changes.
    auto result = planBits(context.getDirtyBits());
    if (result == Plan::Everything)
        return result;

    // Added and removed objects. A nonempty set is the whole record that objects came or went.
    if (!context.getAddedIds().empty() || !context.getRemovedIds().empty())
        return Plan::Everything;

    // Changed objects. Each one reports the bits of its own type.
    for (auto const id : context.getChangedIds()) {
        auto const object = context.get<ContextObject>(id);
        result = std::max(result, planChanged(*object));
        if (result == Plan::Everything)
            return result;
    }
    return result;
}

void OptixContext::pImpl::rebuild() {
    // Rebuild the pipeline first because the SBT packs its program-group headers. Build the new
    // program before dropping the old one, so a failed build keeps the old program.
    auto const spec = OptixProgram::makeSpec(context);
    program = std::make_unique<OptixProgram>(deviceContext, modulePath, spec);

    table.build(context);

    // Instance each visible primitive. Its instance ID is its primitive index, and
    // its instance index is its position among the visible primitives.
    std::vector<OptixAccel::InstanceDesc> instanceDescs;
    std::vector<std::uint32_t> iasIndices(table.primitives.size());
    instanceDescs.reserve(table.primitives.size());
    for (std::size_t index = 0; index < table.primitives.size(); ++index) {
        auto const &primitive = table.primitives[index];
        if (primitive.isHole())
            continue;

        iasIndices[index] = static_cast<std::uint32_t>(instanceDescs.size());
        auto const geometryIndex = primitive.getGeometryIndex();
        auto const bsdfType =
            primitive.hasBSDF() ? table.bsdfs[primitive.getBSDFIndex()].type : BSDFType::Diffuse;
        instanceDescs.push_back({
            .primitiveIndex = static_cast<std::uint32_t>(index),
            .geometryIndex = geometryIndex,
            .sbtOffset = OptixSbt::getInstanceOffset(bsdfType, *table.geometryTypes[geometryIndex]),
            .transform = table.transforms[index],
        });
    }

    // Rebuild in dependency order. GAS consumes the geometry buffers; IAS
    // then consumes the GAS handles and the matching primitive layout.
    // Gather the referenced meshes by geometry index, leaving a null at each hole.
    auto const contextMeshes = context.getObjects<TriangleMesh>();
    std::vector<TriangleMesh const *> meshes(table.geometryTypes.size());
    for (auto const &mesh : contextMeshes) {
        auto const index = context.getIndex<Geometry>(mesh->getContextId());
        if (table.geometryTypes[index])
            meshes[index] = mesh.get();
    }
    geometryPool.build(meshes);
    imageTexturePool.build(context);
    auto const buildInputs = geometryPool.getBuildInputs();
    accel.buildGas(deviceContext, buildInputs);
    accel.buildIas(deviceContext, instanceDescs);
    lightSampler.build(context, imageTexturePool.getImpl(), accel.getSceneRadius());
    primitives.copyFromHost({table.primitives.data(), table.primitives.size()});
    instanceIndices.copyFromHost({iasIndices.data(), iasIndices.size()});
    bsdfs.copyFromHost({table.bsdfs.data(), table.bsdfs.size()});
    edfs.copyFromHost({table.edfs.data(), table.edfs.size()});
    sbt.build(*program);

    // Wait for all queued uploads and builds before returning.
    cudaCheck(cudaStreamSynchronize(getStream()));
    geometryPool.releaseHostStaging();
    LogDebug(
        "OptixContext: built {} geometries and {} visible primitives",
        std::ranges::count_if(
            table.geometryTypes, [](auto const &type) { return type.has_value(); }
        ),
        instanceDescs.size()
    );
}
} // namespace flux
