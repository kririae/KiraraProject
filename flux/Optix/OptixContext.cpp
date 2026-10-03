#include "flux/Optix/OptixContext.h"

#include <optix_stubs.h>

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixGeometryPool.h"
#include "flux/Optix/OptixIas.h"
#include "flux/Optix/OptixImagePool.h"
#include "flux/Optix/OptixImageTexture.h"
#include "flux/Optix/OptixLightSampler.h"
#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixSbt.h"
#include "flux/Optix/OptixTextureObject.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/SceneTableData.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/SmallVector.h"

namespace flux {
struct OptixContext::pImpl final : private CudaStreamMixin {
public:
    pImpl(
        Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
        std::filesystem::path modulePath
    )
        : CudaStreamMixin(stream), context(context), deviceContext(deviceContext),
          modulePath(std::move(modulePath)) {}

    ~pImpl() {
        // Queued copies read host arrays that the members own.
        cudaCheck<false>(cudaStreamSynchronize(getStream()));
    }

    /// Brings every stage up to date with the context, in dependency order.
    void sync();

    Context &context;
    OptixDeviceContext deviceContext;

public:
    // Geometry. Arrays and GASes are memoized by mesh ID. The table is rebuilt on every sync.

    /// Makes the pool hold exactly the live meshes, then places each at its geometry index.
    void syncGeometry();

    OptixGeometryPool geometryPool{deviceContext, getStream()};

    /// Device Impl of each geometry index, kept until the upload completes. An index without a
    /// live mesh holds a default Impl.
    std::vector<Geometry::Impl> geometryStaging;
    /// GAS of each geometry index, for the IAS. An index without a live mesh holds zero.
    std::vector<OptixTraversableHandle> gases;
    DeviceBuffer<Geometry::Impl> geometries{getStream()};

public:
    // Tables. Rebuilt on every sync.

    /// Rebuilds and uploads the primitive, BSDF, and EDF tables.
    void buildTables();

    SceneTableData table;
    DeviceBuffer<Primitive::Impl> primitives{getStream()};
    DeviceBuffer<BSDF::Impl> bsdfs{getStream()};
    DeviceBuffer<EDF::Impl> edfs{getStream()};

public:
    // Textures. Images are memoized by ImageAsset ID. Texture objects and the table are rebuilt
    // on every sync.

    /// Makes the pool hold exactly the images of the live image textures, then places each
    /// texture at its index.
    void syncTextures();

    OptixImagePool imagePool{getStream()};

    /// Texture objects of the table. Declared after the pool, so they are destroyed before the
    /// arrays they sample.
    std::vector<CudaTextureObject> textureObjects;

    /// Texture of each image-texture index, kept until the upload completes.
    std::vector<OptixImageTexture> imageTextureStaging;
    DeviceBuffer<OptixImageTexture> imageTextures{getStream()};

public:
    // Program. Memoized by specialization. The SBT follows it.

    /// Rebuilds the program and SBT when the specialization differs.
    void syncProgram();

    std::filesystem::path modulePath;
    std::unique_ptr<OptixProgram> program;
    OptixSbt sbt{getStream()};

public:
    // IAS. Rebuilt on every sync.

    /// Rebuilds the IAS over the visible primitives.
    void buildIas();

    OptixIas ias{getStream()};

    /// Instance index of each primitive by primitive index, kept until the upload
    /// completes.
    std::vector<std::uint32_t> instanceIndexStaging;
    DeviceBuffer<std::uint32_t> instanceIndices{getStream()};

public:
    // Lights. Rebuilt on every sync.
    OptixLightSampler lightSampler{getStream()};
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
        .traversable = pImpl_->ias.getHandle(),
        .instanceIndices = pImpl_->instanceIndices.data(),
        .table =
            {
                .geometries = pImpl_->geometries.data(),
                .primitives = pImpl_->primitives.data(),
                .bsdfs = pImpl_->bsdfs.data(),
                .edfs = pImpl_->edfs.data(),
            },
        .imageTextures = {.textures = pImpl_->imageTextures.data()},
        .lightSampler = pImpl_->lightSampler.getImpl(),
    };
}

void OptixContext::pImpl::sync() {
    // Meshes and images are memoized by ID, and the program by specialization. The rest is
    // rebuilt in dependency order.
    syncGeometry();
    buildTables();
    syncTextures();
    syncProgram();
    buildIas();
    lightSampler.build(context, {.textures = imageTextures.data()}, ias.getSceneRadius());

    // Wait for the uploads, which read host arrays that the members own.
    cudaCheck(cudaStreamSynchronize(getStream()));
}

void OptixContext::pImpl::syncGeometry() {
    // Find the live meshes that the pool lacks, and the pool's meshes that are no longer live.
    auto const meshes = context.getObjects<TriangleMesh>();
    std::unordered_set<std::size_t> live;
    kira::SmallVector<Ref<TriangleMesh const>> added;
    for (auto const &mesh : meshes) {
        live.insert(mesh->getContextId());
        if (!geometryPool.contains(mesh->getContextId()))
            added.push_back(mesh);
    }
    std::vector<std::size_t> removed;
    for (auto const id : geometryPool.getIds())
        if (!live.contains(id))
            removed.push_back(id);
    geometryPool.remove(removed);
    geometryPool.add({added.data(), added.size()});

    // Place each live mesh at its geometry index. Indices are reused, so the table is rebuilt.
    geometryStaging.assign(context.getIndexLimit<Geometry>(), {});
    gases.assign(geometryStaging.size(), {});
    for (auto const &mesh : meshes) {
        auto const id = mesh->getContextId();
        auto const index = context.getIndex<Geometry>(id);
        geometryStaging[index] = geometryPool.getImpl(id);
        gases[index] = geometryPool.getGas(id);
    }
    geometries.copyFromHost({geometryStaging.data(), geometryStaging.size()});
}

void OptixContext::pImpl::buildTables() {
    table.build(context);
    primitives.copyFromHost({table.primitives.data(), table.primitives.size()});
    bsdfs.copyFromHost({table.bsdfs.data(), table.bsdfs.size()});
    edfs.copyFromHost({table.edfs.data(), table.edfs.size()});
}

void OptixContext::pImpl::syncTextures() {
    // Texture objects sample the arrays, so they go before any array does.
    textureObjects.clear();

    // Find the images that live textures sample and the pool lacks, and the pool's images that no
    // live texture samples.
    auto const textures = context.getObjects<ImageTexture>();
    std::unordered_set<std::size_t> live;
    kira::SmallVector<Ref<ImageAsset const>> added;
    for (auto const &texture : textures) {
        auto const &asset = texture->getImageAsset();
        if (live.insert(asset->getId()).second && !imagePool.contains(asset->getId()))
            added.push_back(asset);
    }
    std::vector<std::size_t> removed;
    for (auto const id : imagePool.getIds())
        if (!live.contains(id))
            removed.push_back(id);
    imagePool.remove(removed);
    imagePool.add({added.data(), added.size()});

    // Create the texture objects of each live texture, and place it at its index.
    textureObjects.reserve(2 * textures.size());
    imageTextureStaging.assign(context.getIndexLimit<ImageTexture>(), {});
    for (auto const &texture : textures) {
        auto const image = imagePool.getImpl(texture->getImageAsset()->getId());
        auto const addressMode = texture->getAddressMode();
        auto const filtered =
            textureObjects
                .emplace_back(createTextureObject(image, addressMode, texture->getFilterMode()))
                .get();
        auto point = filtered;
        if (texture->getFilterMode() != ImageTextureFilterMode::Point)
            point = textureObjects
                        .emplace_back(
                            createTextureObject(image, addressMode, ImageTextureFilterMode::Point)
                        )
                        .get();
        imageTextureStaging[context.getIndex<ImageTexture>(texture->getContextId())] = {
            .texture = filtered,
            .pointTexture = point,
            .componentMapping = texture->getComponentMapping(),
            .componentCount = image.componentCount,
        };
    }
    imageTextures.copyFromHost({imageTextureStaging.data(), imageTextureStaging.size()});
}

void OptixContext::pImpl::syncProgram() {
    auto const spec = OptixProgram::makeSpec(context);
    if (program && program->getSpec() == spec)
        return;

    // The SBT packs the program-group headers, so it follows the program.
    program.reset();
    program = std::make_unique<OptixProgram>(deviceContext, modulePath, spec);
    sbt.build(*program);
}

void OptixContext::pImpl::buildIas() {
    // Instance each visible primitive. Its instance ID is its primitive index, and
    // its instance index is its position among the visible primitives.
    std::vector<OptixIas::InstanceDesc> instanceDescs;
    instanceDescs.reserve(table.primitives.size());
    instanceIndexStaging.assign(table.primitives.size(), 0);
    for (std::size_t index = 0; index < table.primitives.size(); ++index) {
        auto const &primitive = table.primitives[index];
        if (primitive.isHole())
            continue;

        instanceIndexStaging[index] = static_cast<std::uint32_t>(instanceDescs.size());
        auto const bsdfType =
            primitive.hasBSDF() ? table.bsdfs[primitive.getBSDFIndex()].type : BSDFType::Diffuse;

        // Every geometry is a triangle mesh.
        instanceDescs.push_back({
            .primitiveIndex = static_cast<std::uint32_t>(index),
            .gas = gases.at(primitive.getGeometryIndex()),
            .sbtOffset = OptixSbt::getInstanceOffset(bsdfType, GeometryType::TriangleMesh),
            .transform = table.transforms[index],
        });
    }

    ias.build(deviceContext, instanceDescs);
    instanceIndices.copyFromHost({instanceIndexStaging.data(), instanceIndexStaging.size()});
}
} // namespace flux
