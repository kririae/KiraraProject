#include <gtest/gtest.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/plug/registry.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/cameraSchema.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/lightSchema.h>
#include <pxr/imaging/hd/materialBindingSchema.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/materialConnectionSchema.h>
#include <pxr/imaging/hd/materialNetworkSchema.h>
#include <pxr/imaging/hd/materialNodeParameterSchema.h>
#include <pxr/imaging/hd/materialNodeSchema.h>
#include <pxr/imaging/hd/materialSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/meshTopologySchema.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/visibilitySchema.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/usd/sdf/assetPath.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/RenderSettings.h"
#include "flux/IO/Image.h"
#include "flux/IO/ImageIO.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Context.h"
#include "kira/Properties.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
constexpr int width{64};
constexpr int height{32};

SdfPath const cameraPath{"/cam"};
SdfPath const meshPath{"/mesh"};
SdfPath const lightPath{"/light"};
SdfPath const materialPath{"/material"};

/// Returns a perspective camera that looks down -Z from \p z.
HdContainerDataSourceHandle makeCamera(double z) {
    GfMatrix4d transform{1.0};
    transform.SetTranslate(GfVec3d{0.0, 0.0, z});

    return HdRetainedContainerDataSource::New(
        HdCameraSchema::GetSchemaToken(),
        HdCameraSchema::Builder()
            .SetProjection(
                HdRetainedTypedSampledDataSource<TfToken>::New(HdCameraSchemaTokens->perspective)
            )
            .SetFocalLength(HdRetainedTypedSampledDataSource<float>::New(50.0F))
            .SetHorizontalAperture(HdRetainedTypedSampledDataSource<float>::New(20.955F))
            .SetVerticalAperture(HdRetainedTypedSampledDataSource<float>::New(15.2908F))
            .SetClippingRange(HdRetainedTypedSampledDataSource<GfVec2f>::New(GfVec2f{0.1F, 100.0F}))
            .Build(),
        HdXformSchema::GetSchemaToken(),
        HdXformSchema::Builder()
            .SetMatrix(HdRetainedTypedSampledDataSource<GfMatrix4d>::New(transform))
            .Build()
    );
}

/// Returns a mesh with one quad of the vertices \p indices of \p points.
HdContainerDataSourceHandle makeMesh(
    HdTypedSampledDataSource<VtVec3fArray>::Handle const &points,
    VtIntArray const &indices = VtIntArray{0, 1, 2, 3}
) {
    return HdRetainedContainerDataSource::New(
        HdMeshSchema::GetSchemaToken(),
        HdMeshSchema::Builder()
            .SetTopology(
                HdMeshTopologySchema::Builder()
                    .SetFaceVertexCounts(
                        HdRetainedTypedSampledDataSource<VtIntArray>::New(VtIntArray{4})
                    )
                    .SetFaceVertexIndices(
                        HdRetainedTypedSampledDataSource<VtIntArray>::New(indices)
                    )
                    .SetOrientation(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            HdMeshTopologySchemaTokens->rightHanded
                        )
                    )
                    .Build()
            )
            .Build(),
        HdPrimvarsSchema::GetSchemaToken(),
        HdRetainedContainerDataSource::New(
            HdPrimvarsSchemaTokens->points,
            HdPrimvarSchema::Builder()
                .SetPrimvarValue(points)
                .SetInterpolation(
                    HdPrimvarSchema::BuildInterpolationDataSource(HdPrimvarSchemaTokens->vertex)
                )
                .SetRole(HdPrimvarSchema::BuildRoleDataSource(HdPrimvarSchemaTokens->point))
                .Build()
        )
    );
}

HdContainerDataSourceHandle
makeMesh(VtVec3fArray const &points, VtIntArray const &indices = VtIntArray{0, 1, 2, 3}) {
    return makeMesh(HdRetainedTypedSampledDataSource<VtVec3fArray>::New(points), indices);
}

HdContainerDataSourceHandle withFaceVaryingNormals(
    HdContainerDataSourceHandle const &prim,
    HdTypedSampledDataSource<VtVec3fArray>::Handle const &normals
) {
    return HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            HdPrimvarsSchema::GetSchemaToken(),
            HdRetainedContainerDataSource::New(
                HdPrimvarsSchemaTokens->normals,
                HdPrimvarSchema::Builder()
                    .SetPrimvarValue(normals)
                    .SetInterpolation(
                        HdPrimvarSchema::BuildInterpolationDataSource(
                            HdPrimvarSchemaTokens->faceVarying
                        )
                    )
                    .SetRole(HdPrimvarSchema::BuildRoleDataSource(HdPrimvarSchemaTokens->normal))
                    .Build()
            )
        ),
        prim
    );
}

/// Returns the 2x2 quad at z = 0 whose center is at x = \p x. It faces +Z.
VtVec3fArray makeQuad(float x) {
    return {
        GfVec3f{x - 1.0F, -1.0F, 0.0F},
        GfVec3f{x + 1.0F, -1.0F, 0.0F},
        GfVec3f{x + 1.0F, 1.0F, 0.0F},
        GfVec3f{x - 1.0F, 1.0F, 0.0F},
    };
}

HdContainerDataSourceHandle translateAlongX(HdContainerDataSourceHandle const &prim, double x) {
    GfMatrix4d transform{1.0};
    transform.SetTranslate(GfVec3d{x, 0.0, 0.0});

    return HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            HdXformSchema::GetSchemaToken(),
            HdXformSchema::Builder()
                .SetMatrix(HdRetainedTypedSampledDataSource<GfMatrix4d>::New(transform))
                .Build()
        ),
        prim
    );
}

/// Returns \p prim bound to the material at \p materialPath.
HdContainerDataSourceHandle bindMaterial(HdContainerDataSourceHandle const &prim) {
    TfToken const purpose = HdMaterialBindingsSchemaTokens->allPurpose;
    HdDataSourceBaseHandle const binding =
        HdMaterialBindingSchema::Builder()
            .SetPath(HdRetainedTypedSampledDataSource<SdfPath>::New(materialPath))
            .Build();
    return HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            HdMaterialBindingsSchema::GetSchemaToken(),
            HdMaterialBindingsSchema::BuildRetained(1, &purpose, &binding)
        ),
        prim
    );
}

HdContainerDataSourceHandle makeConnection(TfToken const &node, TfToken const &output) {
    return HdMaterialConnectionSchema::Builder()
        .SetUpstreamNodePath(HdRetainedTypedSampledDataSource<TfToken>::New(node))
        .SetUpstreamNodeOutputName(HdRetainedTypedSampledDataSource<TfToken>::New(output))
        .Build();
}

template <typename Value> HdDataSourceBaseHandle makeParameter(Value const &value) {
    return HdMaterialNodeParameterSchema::Builder()
        .SetValue(HdRetainedTypedSampledDataSource<Value>::New(value))
        .Build();
}

/// Returns a material whose \c mtlx surface is a MaterialX surface of a diffuse BSDF of \p color
/// without roughness.
HdContainerDataSourceHandle makeMaterial(GfVec3f const &color) {
    static TfToken const surfaceName{"surface"};
    static TfToken const diffuseName{"diffuse"};
    static TfToken const out{"out"};

    HdDataSourceBaseHandle const diffuseConnection = makeConnection(diffuseName, out);

    return HdRetainedContainerDataSource::New(
        HdMaterialSchema::GetSchemaToken(),
        HdRetainedContainerDataSource::New(
            TfToken{"mtlx"},
            HdMaterialNetworkSchema::Builder()
                .SetNodes(
                    HdRetainedContainerDataSource::New(
                        surfaceName,
                        HdMaterialNodeSchema::Builder()
                            .SetNodeIdentifier(
                                HdRetainedTypedSampledDataSource<TfToken>::New(
                                    TfToken{"ND_surface"}
                                )
                            )
                            .SetInputConnections(
                                HdRetainedContainerDataSource::New(
                                    TfToken{"bsdf"},
                                    HdRetainedSmallVectorDataSource::New(1, &diffuseConnection)
                                )
                            )
                            .Build(),
                        diffuseName,
                        HdMaterialNodeSchema::Builder()
                            .SetNodeIdentifier(
                                HdRetainedTypedSampledDataSource<TfToken>::New(
                                    TfToken{"ND_oren_nayar_diffuse_bsdf"}
                                )
                            )
                            .SetParameters(
                                HdRetainedContainerDataSource::New(
                                    TfToken{"color"}, makeParameter(color), TfToken{"roughness"},
                                    makeParameter(0.0F)
                                )
                            )
                            .Build()
                    )
                )
                .SetTerminals(
                    HdRetainedContainerDataSource::New(
                        HdMaterialTerminalTokens->surface, makeConnection(surfaceName, out)
                    )
                )
                .Build()
        )
    );
}

/// Returns a material like that of \c makeMaterial, whose color is the image \p file.
HdContainerDataSourceHandle makeImageMaterial(SdfAssetPath const &file) {
    static TfToken const surfaceName{"surface"};
    static TfToken const diffuseName{"diffuse"};
    static TfToken const imageName{"image"};
    static TfToken const out{"out"};

    HdDataSourceBaseHandle const diffuseConnection = makeConnection(diffuseName, out);
    HdDataSourceBaseHandle const imageConnection = makeConnection(imageName, out);

    return HdRetainedContainerDataSource::New(
        HdMaterialSchema::GetSchemaToken(),
        HdRetainedContainerDataSource::New(
            TfToken{"mtlx"},
            HdMaterialNetworkSchema::Builder()
                .SetNodes(
                    HdRetainedContainerDataSource::New(
                        surfaceName,
                        HdMaterialNodeSchema::Builder()
                            .SetNodeIdentifier(
                                HdRetainedTypedSampledDataSource<TfToken>::New(
                                    TfToken{"ND_surface"}
                                )
                            )
                            .SetInputConnections(
                                HdRetainedContainerDataSource::New(
                                    TfToken{"bsdf"},
                                    HdRetainedSmallVectorDataSource::New(1, &diffuseConnection)
                                )
                            )
                            .Build(),
                        diffuseName,
                        HdMaterialNodeSchema::Builder()
                            .SetNodeIdentifier(
                                HdRetainedTypedSampledDataSource<TfToken>::New(
                                    TfToken{"ND_oren_nayar_diffuse_bsdf"}
                                )
                            )
                            .SetParameters(
                                HdRetainedContainerDataSource::New(
                                    TfToken{"roughness"}, makeParameter(0.0F)
                                )
                            )
                            .SetInputConnections(
                                HdRetainedContainerDataSource::New(
                                    TfToken{"color"},
                                    HdRetainedSmallVectorDataSource::New(1, &imageConnection)
                                )
                            )
                            .Build(),
                        imageName,
                        HdMaterialNodeSchema::Builder()
                            .SetNodeIdentifier(
                                HdRetainedTypedSampledDataSource<TfToken>::New(
                                    TfToken{"ND_image_color3"}
                                )
                            )
                            .SetParameters(
                                HdRetainedContainerDataSource::New(
                                    TfToken{"file"}, makeParameter(file)
                                )
                            )
                            .Build()
                    )
                )
                .SetTerminals(
                    HdRetainedContainerDataSource::New(
                        HdMaterialTerminalTokens->surface, makeConnection(surfaceName, out)
                    )
                )
                .Build()
        )
    );
}

/// Returns a material whose \c mtlx surface is a \c UsdPreviewSurface, which hdFlux cannot
/// generate.
HdContainerDataSourceHandle makeUnsupportedMaterial() {
    static TfToken const surfaceName{"surface"};

    return HdRetainedContainerDataSource::New(
        HdMaterialSchema::GetSchemaToken(),
        HdRetainedContainerDataSource::New(
            TfToken{"mtlx"},
            HdMaterialNetworkSchema::Builder()
                .SetNodes(
                    HdRetainedContainerDataSource::New(
                        surfaceName, HdMaterialNodeSchema::Builder()
                                         .SetNodeIdentifier(
                                             HdRetainedTypedSampledDataSource<TfToken>::New(
                                                 TfToken{"UsdPreviewSurface"}
                                             )
                                         )
                                         .Build()
                    )
                )
                .SetTerminals(
                    HdRetainedContainerDataSource::New(
                        HdMaterialTerminalTokens->surface,
                        makeConnection(surfaceName, TfToken{"surface"})
                    )
                )
                .Build()
        )
    );
}

/// Holds an array that the test can change after the scene read it.
class EditableArray final : public HdTypedSampledDataSource<VtVec3fArray> {
public:
    explicit EditableArray(VtVec3fArray array) : array_{std::move(array)} {}

    void set(VtVec3fArray array) { array_ = std::move(array); }

    VtValue GetValue(Time) override { return VtValue{array_}; }

    VtVec3fArray GetTypedValue(Time) override { return array_; }

    bool GetContributingSampleTimesForInterval(Time, Time, std::vector<Time> *) override {
        return false;
    }

private:
    VtVec3fArray array_;
};

/// Syncs, prepares, and executes one render pass, as testHdPrman does.
class DrawTask final : public HdTask {
public:
    DrawTask(HdRenderPassSharedPtr pass, HdRenderPassStateSharedPtr state)
        : HdTask{SdfPath::EmptyPath()}, pass_{std::move(pass)}, state_{std::move(state)} {}

    void Sync(HdSceneDelegate *, HdTaskContext *, HdDirtyBits *dirtyBits) override {
        pass_->Sync();
        *dirtyBits = HdChangeTracker::Clean;
    }

    void Prepare(HdTaskContext *, HdRenderIndex *index) override {
        state_->Prepare(index->GetResourceRegistry());
    }

    void Execute(HdTaskContext *) override { pass_->Execute(state_, tags_); }

    TfTokenVector const &GetRenderTags() const override { return tags_; }

private:
    HdRenderPassSharedPtr pass_;
    HdRenderPassStateSharedPtr state_;
    TfTokenVector tags_{HdRenderTagTokens->geometry};
};

/// Renders the camera of a retained scene through the FLux plugin into a color buffer, and a depth
/// buffer that keeps its clear value.
///
/// Eight samples at four per launch converge in two executions.
class RenderTests : public testing::Test {
protected:
    void SetUp() override {
        static std::once_flag registered;
        std::call_once(registered, [] {
            PlugRegistry::GetInstance().RegisterPlugins(HDFLUX_PLUGIN_PATH);
        });

        HdRenderSettingsMap settings;
        settings[HdRenderSettingsTokens->convergedSamplesPerPixel] = VtValue{8};
        settings[HdFluxRenderSettingsTokens->interactiveSamplesPerLaunch] = VtValue{4};

        delegate_ = HdRendererPluginRegistry::GetInstance().CreateRenderDelegate(
            TfToken{"HdFluxRendererPlugin"}, settings
        );
        ASSERT_TRUE(delegate_);

        scene_ = HdRetainedSceneIndex::New();
        scene_->AddPrims({{cameraPath, HdPrimTypeTokens->camera, makeCamera(5.0)}});

        index_.reset(HdRenderIndex::New(delegate_.Get(), HdDriverVector{}));
        index_->InsertSceneIndex(scene_, SdfPath::AbsoluteRootPath());

        buffer_ = static_cast<HdRenderBuffer *>(
            delegate_->CreateBprim(HdPrimTypeTokens->renderBuffer, SdfPath{"/color"})
        );
        ASSERT_TRUE(buffer_->Allocate({width, height, 1}, HdFormatFloat32Vec4, false));

        depthBuffer_ = static_cast<HdRenderBuffer *>(
            delegate_->CreateBprim(HdPrimTypeTokens->renderBuffer, SdfPath{"/depth"})
        );
        ASSERT_TRUE(depthBuffer_->Allocate({width, height, 1}, HdFormatFloat32, false));

        syncIndex();

        HdRenderPassAovBinding color;
        color.aovName = HdAovTokens->color;
        color.renderBuffer = buffer_;
        color.clearValue = VtValue{GfVec4f{0.0F}};

        HdRenderPassAovBinding depth;
        depth.aovName = HdAovTokens->depth;
        depth.renderBuffer = depthBuffer_;
        depth.clearValue = VtValue{1.0F};

        state_ = delegate_->CreateRenderPassState();
        state_->SetFraming(CameraUtilFraming{GfRect2i{GfVec2i{0}, width, height}});
        state_->SetAovBindings({color, depth});
        useCamera();

        pass_ = delegate_->CreateRenderPass(
            index_.get(),
            HdRprimCollection{HdTokens->geometry, HdReprSelector{HdReprTokens->smoothHull}}
        );
        tasks_ = {std::make_shared<DrawTask>(pass_, state_)};
    }

    void TearDown() override {
        if (delegate_ && index_) {
            EXPECT_FALSE(param().HasFailed());

            tasks_.clear();
            pass_.reset();
            delegate_->DestroyBprim(buffer_);
            delegate_->DestroyBprim(depthBuffer_);
            for (auto *const buffer : extraBuffers_)
                delegate_->DestroyBprim(buffer);
        }
    }

    HdFluxRenderParam &param() {
        return *static_cast<HdFluxRenderParam *>(delegate_->GetRenderParam());
    }

    void execute() { engine_.Execute(index_.get(), &tasks_); }

    void syncIndex() {
        HdTaskSharedPtrVector noTasks;
        engine_.Execute(index_.get(), &noTasks);
    }

    void useCamera() {
        auto *const camera =
            static_cast<HdCamera const *>(index_->GetSprim(HdPrimTypeTokens->camera, cameraPath));
        ASSERT_TRUE(camera);
        state_->SetCamera(camera);
    }

    /// Moves the camera along Z. Adding the prim again does not dirty the camera.
    void moveCamera(double z) {
        scene_->AddPrims({{cameraPath, HdPrimTypeTokens->camera, makeCamera(z)}});
        scene_->DirtyPrims({{cameraPath, HdXformSchema::GetDefaultLocator()}});
        syncIndex();
        useCamera();
    }

    /// Binds a new, unallocated buffer to \p aovName besides the bound ones.
    HdRenderBuffer *bindBuffer(TfToken const &aovName) {
        auto *const buffer = static_cast<HdRenderBuffer *>(delegate_->CreateBprim(
            HdPrimTypeTokens->renderBuffer, SdfPath{"/" + aovName.GetString()}
        ));
        extraBuffers_.push_back(buffer);

        HdRenderPassAovBinding binding;
        binding.aovName = aovName;
        binding.renderBuffer = buffer;

        auto bindings = state_->GetAovBindings();
        bindings.push_back(binding);
        state_->SetAovBindings(bindings);
        return buffer;
    }

    /// Binds an allocated normal buffer and returns it.
    HdRenderBuffer *bindNormal() {
        auto *const buffer = bindBuffer(HdAovTokens->normal);
        EXPECT_TRUE(buffer->Allocate({width, height, 1}, HdFormatFloat32Vec3, false));
        return buffer;
    }

    /// Binds an allocated albedo buffer and returns it.
    HdRenderBuffer *bindAlbedo() {
        auto *const buffer = bindBuffer(TfToken{"albedo"});
        EXPECT_TRUE(buffer->Allocate({width, height, 1}, HdFormatFloat32Vec3, false));
        return buffer;
    }

    /// Executes until the pass converges, and checks that the buffer is flagged converged too.
    void executeToConvergence() {
        int count{0};
        do {
            execute();
        } while (!pass_->IsConverged() && ++count < 8);

        EXPECT_TRUE(pass_->IsConverged());
        EXPECT_TRUE(buffer_->IsConverged());
    }

    HdPluginRenderDelegateUniqueHandle delegate_;
    HdRetainedSceneIndexRefPtr scene_;
    std::unique_ptr<HdRenderIndex> index_;
    HdRenderBuffer *buffer_{nullptr};
    HdRenderBuffer *depthBuffer_{nullptr};
    std::vector<HdRenderBuffer *> extraBuffers_;
    HdRenderPassStateSharedPtr state_;
    HdRenderPassSharedPtr pass_;
    HdTaskSharedPtrVector tasks_;
    HdEngine engine_;
};
} // namespace

TEST_F(RenderTests, ConvergesAtTheBufferSize) {
    auto *const unallocated = bindBuffer(HdAovTokens->primId);

    execute();
    EXPECT_FALSE(pass_->IsConverged());
    EXPECT_FALSE(buffer_->IsConverged());
    EXPECT_FALSE(unallocated->IsConverged());

    execute();
    EXPECT_TRUE(pass_->IsConverged());
    EXPECT_TRUE(buffer_->IsConverged());
    EXPECT_TRUE(unallocated->IsConverged());

    // The buffer was cleared to alpha 0, and a rendered pixel is opaque.
    ASSERT_EQ(buffer_->GetWidth(), width);
    ASSERT_EQ(buffer_->GetHeight(), height);
    auto const *const pixels = static_cast<float const *>(buffer_->Map());
    for (int pixel = 0; pixel < width * height; ++pixel)
        ASSERT_EQ(pixels[4 * pixel + 3], 1.0F) << "pixel " << pixel;
    buffer_->Unmap();

    // The depth is not rendered, so it holds its clear value.
    auto const *const depths = static_cast<float const *>(depthBuffer_->Map());
    for (int pixel = 0; pixel < width * height; ++pixel)
        ASSERT_EQ(depths[pixel], 1.0F) << "pixel " << pixel;
    depthBuffer_->Unmap();
}

TEST_F(RenderTests, RestartsWhenTheCameraMoves) {
    executeToConvergence();

    moveCamera(6.0);

    execute();
    EXPECT_FALSE(pass_->IsConverged());
    EXPECT_FALSE(buffer_->IsConverged());

    executeToConvergence();
}

TEST_F(RenderTests, RestartsWhenTheBufferIsResized) {
    executeToConvergence();

    ASSERT_TRUE(buffer_->Allocate({width / 2, height / 2, 1}, HdFormatFloat32Vec4, false));

    execute();
    EXPECT_FALSE(pass_->IsConverged());

    executeToConvergence();
}

TEST_F(RenderTests, WritesTheImageIntoANewBufferThatReplacesTheBoundOne) {
    executeToConvergence();

    delegate_->DestroyBprim(buffer_);
    buffer_ = static_cast<HdRenderBuffer *>(
        delegate_->CreateBprim(HdPrimTypeTokens->renderBuffer, SdfPath{"/color"})
    );
    ASSERT_TRUE(buffer_->Allocate({width, height, 1}, HdFormatFloat32Vec4, false));

    auto bindings = state_->GetAovBindings();
    bindings[0].renderBuffer = buffer_;
    state_->SetAovBindings(bindings);

    execute();
    EXPECT_TRUE(pass_->IsConverged());
    EXPECT_TRUE(buffer_->IsConverged());

    auto const *const pixels = static_cast<float const *>(buffer_->Map());
    for (int pixel = 0; pixel < width * height; ++pixel)
        ASSERT_EQ(pixels[4 * pixel + 3], 1.0F) << "pixel " << pixel;
    buffer_->Unmap();
}

TEST_F(RenderTests, RendersNothingForAZeroTargetAfterTheImageWasRendered) {
    executeToConvergence();

    delegate_->DestroyBprim(buffer_);
    buffer_ = static_cast<HdRenderBuffer *>(
        delegate_->CreateBprim(HdPrimTypeTokens->renderBuffer, SdfPath{"/color"})
    );
    ASSERT_TRUE(buffer_->Allocate({width, height, 1}, HdFormatFloat32Vec4, false));

    auto bindings = state_->GetAovBindings();
    bindings[0].renderBuffer = buffer_;
    state_->SetAovBindings(bindings);

    delegate_->SetRenderSetting(HdRenderSettingsTokens->convergedSamplesPerPixel, VtValue{0});
    moveCamera(6.0);

    execute();
    EXPECT_TRUE(buffer_->IsConverged());

    // The buffer holds its clear value, alpha 0, and not the old image.
    auto const *const pixels = static_cast<float const *>(buffer_->Map());
    for (int pixel = 0; pixel < width * height; ++pixel)
        ASSERT_EQ(pixels[4 * pixel + 3], 0.0F) << "pixel " << pixel;
    buffer_->Unmap();
}

TEST_F(RenderTests, AppliesAChangedClearValueToABufferThatShowsNoImage) {
    executeToConvergence();

    auto bindings = state_->GetAovBindings();
    bindings[1].clearValue = VtValue{0.5F};
    state_->SetAovBindings(bindings);

    execute();

    auto const *const depths = static_cast<float const *>(depthBuffer_->Map());
    for (int pixel = 0; pixel < width * height; ++pixel)
        ASSERT_EQ(depths[pixel], 0.5F) << "pixel " << pixel;
    depthBuffer_->Unmap();
}

TEST_F(RenderTests, ConvergesInOneExecutionWhenTheHostIsNotInteractive) {
    delegate_->SetRenderSetting(HdRenderSettingsTokens->enableInteractive, VtValue{false});

    execute();

    EXPECT_TRUE(pass_->IsConverged());
    EXPECT_TRUE(buffer_->IsConverged());
}

TEST_F(RenderTests, RestartsWhenTheSceneIsEdited) {
    executeToConvergence();

    auto const context = param().GetContext();
    context->setActiveSampler(context->create<flux::IndependentSampler>(kira::Properties{}));

    execute();
    EXPECT_FALSE(pass_->IsConverged());

    executeToConvergence();
}

TEST_F(RenderTests, RendersMoreWhenTheSettingsRaiseTheTarget) {
    executeToConvergence();

    delegate_->SetRenderSetting(HdRenderSettingsTokens->convergedSamplesPerPixel, VtValue{16});

    execute();
    EXPECT_FALSE(pass_->IsConverged());

    executeToConvergence();
}

namespace {
/// Returns the vector that \p buffer holds at \p column of the center row.
GfVec3f vectorAt(HdRenderBuffer *buffer, int column) {
    auto const *const pixels = static_cast<GfVec3f const *>(buffer->Map());
    auto const vector = pixels[(height / 2) * width + column];
    buffer->Unmap();
    return vector;
}

GfVec3f centerNormal(HdRenderBuffer *buffer) { return vectorAt(buffer, width / 2); }
} // namespace

TEST_F(RenderTests, WritesTheNormalOfAMeshThatCoversThePixel) {
    auto *const normal = bindNormal();
    scene_->AddPrims({{meshPath, HdPrimTypeTokens->mesh, makeMesh(makeQuad(0.0F))}});

    executeToConvergence();

    EXPECT_NEAR(centerNormal(normal)[2], 1.0F, 1e-4F);
}

TEST_F(RenderTests, MovesTheHitWhenOnlyThePointsChange) {
    auto *const normal = bindNormal();
    auto const points = std::make_shared<EditableArray>(makeQuad(0.0F));
    scene_->AddPrims({{meshPath, HdPrimTypeTokens->mesh, makeMesh(points)}});
    executeToConvergence();
    EXPECT_NEAR(centerNormal(normal)[2], 1.0F, 1e-4F);

    points->set(makeQuad(1.5F));
    scene_->DirtyPrims({{meshPath, HdPrimvarsSchema::GetPointsLocator()}});
    executeToConvergence();

    EXPECT_NEAR(vectorAt(normal, 48)[2], 1.0F, 1e-4F);
    EXPECT_EQ(centerNormal(normal), GfVec3f{0.0F});
}

TEST_F(RenderTests, RendersTheNewNormalsWhenOnlyTheNormalsChange) {
    auto *const normal = bindNormal();
    auto const normals =
        std::make_shared<EditableArray>(VtVec3fArray(4, GfVec3f{0.0F, 0.6F, 0.8F}));
    scene_->AddPrims(
        {{meshPath, HdPrimTypeTokens->mesh,
          withFaceVaryingNormals(makeMesh(makeQuad(0.0F)), normals)}}
    );
    executeToConvergence();
    EXPECT_NEAR(centerNormal(normal)[1], 0.6F, 1e-4F);
    EXPECT_NEAR(centerNormal(normal)[2], 0.8F, 1e-4F);

    normals->set(VtVec3fArray(4, GfVec3f{0.6F, 0.0F, 0.8F}));
    scene_->DirtyPrims({{meshPath, HdPrimvarsSchema::GetNormalsLocator()}});
    executeToConvergence();

    EXPECT_NEAR(centerNormal(normal)[0], 0.6F, 1e-4F);
    EXPECT_NEAR(centerNormal(normal)[2], 0.8F, 1e-4F);
}

TEST_F(RenderTests, RendersAMeshThatGetsPointsAfterItHadNone) {
    auto *const normal = bindNormal();
    scene_->AddPrims({{meshPath, HdPrimTypeTokens->mesh, makeMesh(VtVec3fArray{})}});
    executeToConvergence();
    EXPECT_EQ(centerNormal(normal), GfVec3f{0.0F});

    scene_->AddPrims({{meshPath, HdPrimTypeTokens->mesh, makeMesh(makeQuad(0.0F))}});
    scene_->DirtyPrims({{meshPath, HdPrimvarsSchema::GetDefaultLocator()}});
    executeToConvergence();

    EXPECT_NEAR(centerNormal(normal)[2], 1.0F, 1e-4F);
}

TEST_F(RenderTests, LeavesTheBackgroundWhenTheMeshIsRemoved) {
    auto *const normal = bindNormal();
    scene_->AddPrims({{meshPath, HdPrimTypeTokens->mesh, makeMesh(makeQuad(0.0F))}});
    executeToConvergence();

    scene_->RemovePrims({{meshPath}});
    executeToConvergence();

    EXPECT_EQ(centerNormal(normal), GfVec3f{0.0F});
}

namespace {
constexpr float fallbackAlbedo{0.18F};

GfVec3f const red{0.75F, 0.25F, 0.0F};
GfVec3f const green{0.0F, 0.5F, 0.25F};

void expectFallbackAlbedo(GfVec3f const &albedo) {
    for (int component = 0; component < 3; ++component)
        EXPECT_EQ(albedo[component], fallbackAlbedo);
}
} // namespace

TEST_F(RenderTests, ShadesAMeshWithoutMaterialWithTheFallback) {
    auto *const albedo = bindAlbedo();
    scene_->AddPrims({{meshPath, HdPrimTypeTokens->mesh, makeMesh(makeQuad(0.0F))}});

    executeToConvergence();

    expectFallbackAlbedo(vectorAt(albedo, width / 2));
}

TEST_F(RenderTests, RendersTheNewValueWhenOnlyAMaterialValueChanges) {
    auto *const albedo = bindAlbedo();
    scene_->AddPrims(
        {{materialPath, HdPrimTypeTokens->material, makeMaterial(red)},
         {meshPath, HdPrimTypeTokens->mesh, bindMaterial(makeMesh(makeQuad(0.0F)))}}
    );
    executeToConvergence();
    EXPECT_EQ(vectorAt(albedo, width / 2), red);

    scene_->AddPrims({{materialPath, HdPrimTypeTokens->material, makeMaterial(green)}});
    scene_->DirtyPrims({{materialPath, HdMaterialSchema::GetDefaultLocator()}});
    executeToConvergence();

    EXPECT_EQ(vectorAt(albedo, width / 2), green);
}

TEST_F(RenderTests, RendersTheImageAtTheResolvedPathOfAnAssetPath) {
    auto const imagePath = std::filesystem::path{HDFLUX_TEST_OUTPUT_DIR} / "image.exr";
    auto const pixel = std::array{0.5F, 0.25F, 0.75F};
    auto const componentNames = std::array<std::string_view, 3>{"R", "G", "B"};
    flux::writeImage(
        imagePath,
        {
            .pixels = std::as_bytes(std::span{pixel}),
            .extent = {1, 1},
            .componentType = flux::ImageComponentType::Float32,
            .componentCount = static_cast<std::uint8_t>(componentNames.size()),
        },
        {
            .outputComponentType = flux::ImageComponentType::Float32,
            .componentNames = componentNames,
        }
    );

    auto *const albedo = bindAlbedo();
    scene_->AddPrims(
        {{materialPath, HdPrimTypeTokens->material,
          makeImageMaterial(SdfAssetPath{"authored/missing.exr", imagePath.string()})},
         {meshPath, HdPrimTypeTokens->mesh, bindMaterial(makeMesh(makeQuad(0.0F)))}}
    );

    executeToConvergence();

    EXPECT_EQ(vectorAt(albedo, width / 2), GfVec3f(pixel[0], pixel[1], pixel[2]));
}

TEST_F(RenderTests, RendersTheNewMaterialWhenAnEditMakesAnUnsupportedMaterialGenerate) {
    auto *const albedo = bindAlbedo();
    scene_->AddPrims(
        {{materialPath, HdPrimTypeTokens->material, makeUnsupportedMaterial()},
         {meshPath, HdPrimTypeTokens->mesh, bindMaterial(makeMesh(makeQuad(0.0F)))}}
    );
    executeToConvergence();
    expectFallbackAlbedo(vectorAt(albedo, width / 2));

    scene_->AddPrims({{materialPath, HdPrimTypeTokens->material, makeMaterial(red)}});
    scene_->DirtyPrims({{materialPath, HdMaterialSchema::GetDefaultLocator()}});
    executeToConvergence();
    EXPECT_EQ(vectorAt(albedo, width / 2), red);

    scene_->AddPrims({{materialPath, HdPrimTypeTokens->material, makeUnsupportedMaterial()}});
    scene_->DirtyPrims({{materialPath, HdMaterialSchema::GetDefaultLocator()}});
    executeToConvergence();

    expectFallbackAlbedo(vectorAt(albedo, width / 2));
}

TEST_F(RenderTests, KeepsTheMaterialOfAMeshThatLosesItsPointsAndGetsThemAgain) {
    auto *const albedo = bindAlbedo();
    auto const points = std::make_shared<EditableArray>(makeQuad(0.0F));
    scene_->AddPrims(
        {{materialPath, HdPrimTypeTokens->material, makeMaterial(red)},
         {meshPath, HdPrimTypeTokens->mesh, bindMaterial(makeMesh(points))}}
    );
    executeToConvergence();
    EXPECT_EQ(vectorAt(albedo, width / 2), red);

    points->set(VtVec3fArray{});
    scene_->DirtyPrims({{meshPath, HdPrimvarsSchema::GetPointsLocator()}});
    executeToConvergence();
    EXPECT_EQ(vectorAt(albedo, width / 2), GfVec3f{0.0F});

    points->set(makeQuad(0.0F));
    scene_->DirtyPrims({{meshPath, HdPrimvarsSchema::GetPointsLocator()}});
    executeToConvergence();

    EXPECT_EQ(vectorAt(albedo, width / 2), red);
}

TEST_F(RenderTests, KeepsRenderingWhenTheMeshContentIsInvalid) {
    auto *const normal = bindNormal();
    scene_->AddPrims(
        {{meshPath, HdPrimTypeTokens->mesh, makeMesh(makeQuad(0.0F), VtIntArray{0, 1, 2, 5})}}
    );

    executeToConvergence();

    EXPECT_EQ(centerNormal(normal), GfVec3f{0.0F});
}

TEST_F(RenderTests, KeepsTheTransformOfAMeshThatLosesItsPointsAndGetsThemAgain) {
    auto *const normal = bindNormal();
    auto const points = std::make_shared<EditableArray>(makeQuad(0.0F));
    scene_->AddPrims({{meshPath, HdPrimTypeTokens->mesh, translateAlongX(makeMesh(points), 1.5)}});
    executeToConvergence();
    EXPECT_NEAR(vectorAt(normal, 48)[2], 1.0F, 1e-4F);
    EXPECT_EQ(centerNormal(normal), GfVec3f{0.0F});

    points->set(VtVec3fArray{});
    scene_->DirtyPrims({{meshPath, HdPrimvarsSchema::GetPointsLocator()}});
    executeToConvergence();
    EXPECT_EQ(vectorAt(normal, 48), GfVec3f{0.0F});

    points->set(makeQuad(0.0F));
    scene_->DirtyPrims({{meshPath, HdPrimvarsSchema::GetPointsLocator()}});
    executeToConvergence();

    EXPECT_NEAR(vectorAt(normal, 48)[2], 1.0F, 1e-4F);
    EXPECT_EQ(centerNormal(normal), GfVec3f{0.0F});
}

namespace {
/// It emits toward +Z.
HdContainerDataSourceHandle makeNormalizedLight(float size, double scale) {
    return HdRetainedContainerDataSource::New(
        HdLightSchema::GetSchemaToken(),
        HdRetainedContainerDataSource::New(
            HdLightTokens->normalize, HdRetainedTypedSampledDataSource<bool>::New(true),
            HdLightTokens->width, HdRetainedTypedSampledDataSource<float>::New(size),
            HdLightTokens->height, HdRetainedTypedSampledDataSource<float>::New(size)
        ),
        HdXformSchema::GetSchemaToken(),
        HdXformSchema::Builder()
            .SetMatrix(
                HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                    GfMatrix4d{GfVec4d{-scale, scale, -scale, 1.0}}
                )
            )
            .Build()
    );
}

float centerChannel(HdRenderBuffer *buffer, int channel) {
    auto const *const pixels = static_cast<float const *>(buffer->Map());
    auto const value = pixels[4 * ((height / 2) * width + width / 2) + channel];
    buffer->Unmap();
    return value;
}
} // namespace

TEST_F(RenderTests, DividesTheRadianceOfAnEditedNormalizedLightByItsNewArea) {
    scene_->AddPrims({{lightPath, HdPrimTypeTokens->rectLight, makeNormalizedLight(1.0F, 1.0)}});
    executeToConvergence();
    EXPECT_EQ(centerChannel(buffer_, 0), 1.0F);

    scene_->AddPrims({{lightPath, HdPrimTypeTokens->rectLight, makeNormalizedLight(2.0F, 2.0)}});
    scene_->DirtyPrims(
        {{lightPath, HdLightSchema::GetDefaultLocator()},
         {lightPath, HdXformSchema::GetDefaultLocator()}}
    );
    executeToConvergence();
    EXPECT_EQ(centerChannel(buffer_, 0), 0.0625F);
}

namespace {
HdContainerDataSourceHandle makeDomeLight(std::string const &file, bool visible) {
    return HdRetainedContainerDataSource::New(
        HdLightSchema::GetSchemaToken(),
        HdRetainedContainerDataSource::New(
            HdLightTokens->textureFile,
            HdRetainedTypedSampledDataSource<SdfAssetPath>::New(SdfAssetPath{file})
        ),
        HdVisibilitySchema::GetSchemaToken(),
        HdVisibilitySchema::Builder()
            .SetVisibility(HdRetainedTypedSampledDataSource<bool>::New(visible))
            .Build()
    );
}
} // namespace

TEST_F(RenderTests, LeavesTheBackgroundWhenTheDomeLightIsHidden) {
    scene_->AddPrims({{lightPath, HdPrimTypeTokens->domeLight, makeDomeLight("", true)}});
    executeToConvergence();
    EXPECT_EQ(centerChannel(buffer_, 0), 1.0F);

    scene_->AddPrims({{lightPath, HdPrimTypeTokens->domeLight, makeDomeLight("", false)}});
    scene_->DirtyPrims({{lightPath, HdVisibilitySchema::GetDefaultLocator()}});
    executeToConvergence();

    EXPECT_EQ(centerChannel(buffer_, 0), 0.0F);
}

TEST_F(RenderTests, LeavesTheBackgroundWhenTheDomeLightIsRemoved) {
    scene_->AddPrims({{lightPath, HdPrimTypeTokens->domeLight, makeDomeLight("", true)}});
    executeToConvergence();
    EXPECT_EQ(centerChannel(buffer_, 0), 1.0F);

    scene_->RemovePrims({{lightPath}});
    executeToConvergence();

    EXPECT_EQ(centerChannel(buffer_, 0), 0.0F);
}

TEST_F(RenderTests, ShowsTheDefaultTextureWhenTheDomeLightLosesItsFile) {
    scene_->AddPrims(
        {{lightPath, HdPrimTypeTokens->domeLight, makeDomeLight("/nonexistent.png", true)}}
    );
    executeToConvergence();
    EXPECT_EQ(centerChannel(buffer_, 1), 0.0F);

    scene_->AddPrims({{lightPath, HdPrimTypeTokens->domeLight, makeDomeLight("", true)}});
    scene_->DirtyPrims({{lightPath, HdLightSchema::GetDefaultLocator()}});
    executeToConvergence();

    EXPECT_EQ(centerChannel(buffer_, 1), 1.0F);
}
