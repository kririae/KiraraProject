#include "flux/Optix/OptixContext.h"

#include <optix_stubs.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixGeometryPool.h"
#include "flux/Optix/OptixIas.h"
#include "flux/Optix/OptixImagePool.h"
#include "flux/Optix/OptixImageTexture.h"
#include "flux/Optix/OptixLightSampler.h"
#include "flux/Optix/OptixMegaKernelModule.h"
#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixSbt.h"
#include "flux/Optix/OptixShaderBinding.h"
#include "flux/Optix/OptixShaderPool.h"
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
        Context &context, OptixDeviceContext deviceContext, DeletionQueue &deletionQueue,
        std::filesystem::path modulePath
    )
        : CudaStreamMixin(deletionQueue.getStream()), context(context),
          deviceContext(deviceContext), deletionQueue(deletionQueue),
          modulePath(std::move(modulePath)) {}

    ~pImpl() {
        // Work on the stream may still use the texture objects and the program, and they are not
        // deleted through the deletion queue.
        cudaCheck<false>(cudaStreamSynchronize(getStream()));
    }

    /// Brings every stage up to date with the context, in dependency order.
    void sync();

    Context &context;
    OptixDeviceContext deviceContext;
    DeletionQueue &deletionQueue;

public:
    // Geometry. Arrays and GASes are memoized by mesh ID. The table is rebuilt on every sync.

    /// Makes the pool hold exactly the live meshes, then places each at its geometry index.
    void syncGeometry();

    OptixGeometryPool geometryPool{deviceContext, deletionQueue};

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

    OptixImagePool imagePool{deletionQueue};

    /// Texture objects of the table. Declared after the pool, so they are destroyed before the
    /// arrays they sample.
    std::vector<CudaTextureObject> textureObjects;

    DeviceBuffer<OptixImageTexture> imageTextures{getStream()};

public:
    // Shaders. Modules are memoized by ShaderAsset ID. The rest is rebuilt on every sync.

    /// Makes the pool hold exactly the assets of the live shader BSDFs and shader EDFs, gives the
    /// assets of the BSDFs a slot and those of the EDFs an EDF slot, and uploads the buffers that
    /// the callables read.
    void syncShaders();

    OptixShaderPool shaderPool{deviceContext};

    /// The ID of the asset at each slot.
    std::vector<std::size_t> shaderBSDFSlotIds;

    /// The ID of the asset at each EDF slot.
    std::vector<std::size_t> shaderEDFSlotIds;

    DeviceBuffer<std::uint32_t> shaderWords{getStream()};
    DeviceBuffer<std::uint32_t> shaderTextureIndices{getStream()};
    DeviceBuffer<OptixShaderBinding> shaderBSDFBindings{getStream()};
    DeviceBuffer<OptixShaderBinding> shaderEDFBindings{getStream()};
    unsigned int shaderBSDFSlotBits{};

public:
    // Megakernel module. Memoized by specialization. The program and the SBT follow it.

    /// Rebuilds the module, the program, and the SBT when the specialization differs.
    void syncProgram();

    std::filesystem::path modulePath;
    std::shared_ptr<OptixMegaKernelModule const> megaKernelModule;
    std::unique_ptr<OptixProgram> program;
    OptixSbt sbt{deletionQueue};

public:
    // IAS. Rebuilt on every sync.

    /// Rebuilds the IAS over the visible primitives.
    void buildIas();

    OptixIas ias{deletionQueue};
    DeviceBuffer<std::uint32_t> instanceIndices{getStream()};

public:
    // Lights.
    OptixLightSampler lightSampler{deletionQueue};
};

OptixContext::OptixContext(
    Context &context, OptixDeviceContext deviceContext, DeletionQueue &deletionQueue,
    std::filesystem::path const &modulePath
)
    : pImpl_(std::make_unique<pImpl>(context, deviceContext, deletionQueue, modulePath)) {}

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

OptixMegaKernelModuleSpec const &OptixContext::getMegaKernelModuleSpec() const noexcept {
    return pImpl_->megaKernelModule->getSpec();
}

OptixCallableLaunchParams OptixContext::getCallableLaunchParams() const noexcept {
    return {
        .imageTextures = {.textures = pImpl_->imageTextures.data()},
        .words = pImpl_->shaderWords.data(),
        .textureIndices = pImpl_->shaderTextureIndices.data(),
    };
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
        .shaderBSDFBindings = pImpl_->shaderBSDFBindings.data(),
        .shaderEDFBindings = pImpl_->shaderEDFBindings.data(),
        .shaderBSDFSlotBits = pImpl_->shaderBSDFSlotBits,
        .lightSampler = pImpl_->lightSampler.getImpl(),
    };
}

void OptixContext::pImpl::sync() {
    syncGeometry();
    buildTables();
    syncShaders();
    syncTextures();
    syncProgram();
    buildIas();
    lightSampler.sync(context, {.textures = imageTextures.data()}, ias.getSceneRadius());
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

    // An index without a live mesh holds a default Impl. Indices are reused, so every sync rebuilds
    // the table.
    std::vector<Geometry::Impl> hostGeometries(context.getIndexLimit<Geometry>());
    gases.assign(hostGeometries.size(), {});
    for (auto const &mesh : meshes) {
        auto const id = mesh->getContextId();
        auto const index = context.getIndex<Geometry>(id);
        hostGeometries[index] = geometryPool.getImpl(id);
        gases[index] = geometryPool.getGas(id);
    }

    geometries.copyFromHost({hostGeometries.data(), hostGeometries.size()}, deletionQueue);
}

void OptixContext::pImpl::buildTables() {
    table.build(context);
    primitives.copyFromHost({table.primitives.data(), table.primitives.size()}, deletionQueue);
    bsdfs.copyFromHost({table.bsdfs.data(), table.bsdfs.size()}, deletionQueue);
    edfs.copyFromHost({table.edfs.data(), table.edfs.size()}, deletionQueue);
}

void OptixContext::pImpl::syncShaders() {
    std::map<std::size_t, Ref<ShaderAsset const>> bsdfAssets;
    for (auto const &bsdf : context.getObjects<BSDF>())
        if (bsdf->getType() == BSDFType::Shader) {
            auto const &asset = static_cast<ShaderBSDF const &>(*bsdf).getAsset();
            bsdfAssets.try_emplace(asset->getId(), asset);
        }
    std::map<std::size_t, Ref<ShaderAsset const>> edfAssets;
    for (auto const &edf : context.getObjects<EDF>())
        if (edf->getType() == EDFType::Shader) {
            auto const &asset = static_cast<ShaderEDF const &>(*edf).getAsset();
            edfAssets.try_emplace(asset->getId(), asset);
        }

    // An asset gets a slot among those of the BSDFs, an EDF slot among those of the EDFs, or both.
    std::unordered_map<std::size_t, std::uint32_t> bsdfSlotsById;
    shaderBSDFSlotIds.clear();
    for (auto const &[id, asset] : bsdfAssets) {
        bsdfSlotsById.emplace(id, static_cast<std::uint32_t>(shaderBSDFSlotIds.size()));
        shaderBSDFSlotIds.push_back(id);
    }
    std::unordered_map<std::size_t, std::uint32_t> edfSlotsById;
    shaderEDFSlotIds.clear();
    for (auto const &[id, asset] : edfAssets) {
        edfSlotsById.emplace(id, static_cast<std::uint32_t>(shaderEDFSlotIds.size()));
        shaderEDFSlotIds.push_back(id);
    }

    auto live = bsdfAssets;
    live.insert(edfAssets.begin(), edfAssets.end());
    kira::SmallVector<Ref<ShaderAsset const>> added;
    for (auto const &[id, asset] : live)
        if (!shaderPool.contains(id))
            added.push_back(asset);

    std::vector<std::uint32_t> hostWords;
    std::vector<std::uint32_t> hostTextureIndices;
    auto const appendData = [&](std::uint32_t slot, std::vector<std::uint32_t> const &words,
                                std::vector<Ref<ImageTexture const>> const &textures) {
        OptixShaderBinding binding{
            .slot = slot,
            .wordOffset = static_cast<std::uint32_t>(hostWords.size()),
            .textureOffset = static_cast<std::uint32_t>(hostTextureIndices.size()),
        };
        hostWords.insert(hostWords.end(), words.begin(), words.end());
        for (auto const &texture : textures)
            hostTextureIndices.push_back(context.getIndex<ImageTexture>(texture->getContextId()));
        return binding;
    };

    std::vector<OptixShaderBinding> hostBSDFBindings(context.getIndexLimit<BSDF>());
    for (auto const &bsdf : context.getObjects<BSDF>()) {
        if (bsdf->getType() != BSDFType::Shader)
            continue;

        auto const &shaderBSDF = static_cast<ShaderBSDF const &>(*bsdf);
        hostBSDFBindings[context.getIndex<BSDF>(bsdf->getContextId())] = appendData(
            bsdfSlotsById.at(shaderBSDF.getAsset()->getId()), shaderBSDF.getWords(),
            shaderBSDF.getTextures()
        );
    }

    // The EDF callables follow the BSDF callables in the SBT.
    std::vector<OptixShaderBinding> hostEDFBindings(context.getIndexLimit<EDF>());
    for (auto const &edf : context.getObjects<EDF>()) {
        if (edf->getType() != EDFType::Shader)
            continue;

        auto const &shaderEDF = static_cast<ShaderEDF const &>(*edf);
        hostEDFBindings[context.getIndex<EDF>(edf->getContextId())] = appendData(
            static_cast<std::uint32_t>(shaderBSDFSlotIds.size()) +
                edfSlotsById.at(shaderEDF.getAsset()->getId()),
            shaderEDF.getWords(), shaderEDF.getTextures()
        );
    }

    std::vector<std::size_t> removed;
    for (auto const id : shaderPool.getIds())
        if (!live.contains(id))
            removed.push_back(id);
    shaderPool.remove(removed);
    shaderPool.add({added.data(), added.size()});

    shaderWords.copyFromHost({hostWords.data(), hostWords.size()}, deletionQueue);
    shaderTextureIndices.copyFromHost(
        {hostTextureIndices.data(), hostTextureIndices.size()}, deletionQueue
    );
    shaderBSDFBindings.copyFromHost(
        {hostBSDFBindings.data(), hostBSDFBindings.size()}, deletionQueue
    );
    shaderEDFBindings.copyFromHost({hostEDFBindings.data(), hostEDFBindings.size()}, deletionQueue);
    shaderBSDFSlotBits =
        shaderBSDFSlotIds.size() > 1
            ? static_cast<unsigned int>(std::bit_width(shaderBSDFSlotIds.size() - 1))
            : 0U;
}

void OptixContext::pImpl::syncTextures() {
    // Queued launches may still sample the old texture objects.
    if (!textureObjects.empty())
        deletionQueue.deleteLater(
            std::make_shared<std::vector<CudaTextureObject>>(std::move(textureObjects))
        );

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
    std::vector<OptixImageTexture> hostImageTextures(context.getIndexLimit<ImageTexture>());
    for (auto const &texture : textures) {
        auto const image = imagePool.getImpl(texture->getImageAsset()->getId());
        auto const &addressModes = texture->getAddressModes();
        auto const filtered =
            textureObjects
                .emplace_back(createTextureObject(image, addressModes, texture->getFilterMode()))
                .get();
        auto point = filtered;
        if (texture->getFilterMode() != ImageTextureFilterMode::Point)
            point = textureObjects
                        .emplace_back(
                            createTextureObject(image, addressModes, ImageTextureFilterMode::Point)
                        )
                        .get();
        auto const &fallback = texture->getFallback();
        auto const &scale = texture->getScale();
        auto const &bias = texture->getBias();
        hostImageTextures[context.getIndex<ImageTexture>(texture->getContextId())] = {
            .texture = filtered,
            .pointTexture = point,
            .componentMapping = texture->getComponentMapping(),
            .placeholder = texture->getImageAsset()->isPlaceholder(),
            .componentCount = image.componentCount,
            .fallback = {fallback.x(), fallback.y(), fallback.z(), fallback.w()},
            .scale = {scale.x(), scale.y(), scale.z(), scale.w()},
            .bias = {bias.x(), bias.y(), bias.z(), bias.w()},
        };
    }

    imageTextures.copyFromHost({hostImageTextures.data(), hostImageTextures.size()}, deletionQueue);
}

void OptixContext::pImpl::syncProgram() {
    // The program links the module of each slot and of each EDF slot, so it follows the slots.
    auto const spec = OptixMegaKernelModule::makeSpec(context);
    auto const getModules = [&](std::vector<std::size_t> const &ids) {
        std::vector<std::shared_ptr<OptixShaderModule const>> modules;
        modules.reserve(ids.size());
        for (auto const id : ids)
            modules.push_back(shaderPool.getModule(id));
        return modules;
    };
    auto bsdfModules = getModules(shaderBSDFSlotIds);
    auto edfModules = getModules(shaderEDFSlotIds);
    if (program && megaKernelModule->getSpec() == spec &&
        std::ranges::equal(program->getBSDFModules(), bsdfModules) &&
        std::ranges::equal(program->getEDFModules(), edfModules))
        return;

    if (program)
        deletionQueue.deleteLater(std::shared_ptr<OptixProgram const>(std::move(program)));
    if (!megaKernelModule || megaKernelModule->getSpec() != spec)
        megaKernelModule = std::make_shared<OptixMegaKernelModule>(deviceContext, modulePath, spec);
    program =
        std::make_unique<OptixProgram>(deviceContext, megaKernelModule, bsdfModules, edfModules);
    sbt.build(*megaKernelModule, bsdfModules, edfModules);
}

void OptixContext::pImpl::buildIas() {
    // Instance each visible primitive. Its instance ID is its primitive index, and
    // its instance index is its position among the visible primitives.
    std::vector<OptixIas::InstanceDesc> instanceDescs;
    instanceDescs.reserve(table.primitives.size());
    std::vector<std::uint32_t> instanceIndexByPrimitive(table.primitives.size());
    for (std::size_t index = 0; index < table.primitives.size(); ++index) {
        auto const &primitive = table.primitives[index];
        if (primitive.isHole())
            continue;

        instanceIndexByPrimitive[index] = static_cast<std::uint32_t>(instanceDescs.size());
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

    instanceIndices.copyFromHost(
        {instanceIndexByPrimitive.data(), instanceIndexByPrimitive.size()}, deletionQueue
    );
}
} // namespace flux
