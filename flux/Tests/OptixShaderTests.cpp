#include <MaterialXCore/Document.h> // NOLINT(llvm-include-order)
#include <MaterialXFormat/XmlIo.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "SceneUtils.h"
#include "TestUtils.h"
#include "flux/Mtlx/MtlxBSDFGenerator.h"
#include "flux/Optix/OptixAccumulationBuffer.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/Texture.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

namespace {
constexpr auto resolution = flux::Vec2u{16, 16};
constexpr std::uint32_t samples = 16;
constexpr float tolerance = 0.012F;

/// \brief A diffuse reflector with no roughness.
constexpr auto smoothDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF">
    <input name="color" type="color3" value="0.8, 0.2, 0.2" />
    <input name="roughness" type="float" value="0" />
  </oren_nayar_diffuse_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="fg" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief The reflector, with its color read from the words.
[[nodiscard]] flux::MtlxBSDFGenerator::Instance generateSmoothDiffuse() {
    auto const generator = flux::MtlxBSDFGenerator{FLUX_MATERIALX_DIR};
    auto document = MaterialX::createDocument();
    MaterialX::readFromXmlString(document, smoothDocument);
    auto const interactive = std::array<std::string, 1>{"fg/color"};
    auto result = generator.generate(document, "M", {}, interactive);
    if (!result.instance)
        throw kira::Anyhow{"the material generated nothing"};
    return *std::move(result.instance);
}

/// \brief A triangle that fills the view of \c renderMean's camera, with the texture coordinates
/// (0.5 + x / 2, 0.5 + y / 4) at each point (x, y, 0).
[[nodiscard]] flux::TriangleMesh::Data largeTriangleData() {
    return {
        .vertices = flux::test::sharedBuffer(
            flux::Vec3f{-10.0F, -10.0F, 0.0F}, flux::Vec3f{10.0F, -10.0F, 0.0F},
            flux::Vec3f{0.0F, 10.0F, 0.0F}
        ),
        .triangles = flux::test::sharedBuffer(flux::Vec3u{0, 1, 2}),
        .texCoords = flux::test::sharedBuffer(
            flux::Vec2f{-4.5F, -2.0F}, flux::Vec2f{5.5F, -2.0F}, flux::Vec2f{0.5F, 3.0F}
        ),
    };
}

/// \brief The properties of an image texture whose value is \p color at every uv.
[[nodiscard]] kira::Properties constantTexture(flux::Vec4f const &color) {
    kira::Properties properties;
    properties.set(
        "path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Shared/Textures/Checker.png"
    );
    properties.set("scale", flux::Vec4f{0.0F});
    properties.set("bias", color);
    return properties;
}

/// \brief Renders the triangle with the shader of \p spec and \p words under a constant
/// environment, and returns the mean of the pixels.
///
/// The shader samples \p textures. An image texture that nothing samples precedes them, so that
/// the image-texture index of a texture is not its slot. A shader BSDF of the same source, which no
/// primitive uses, binds black textures before and after the visible one.
[[nodiscard]] flux::Spectrum renderMean(
    flux::ShaderAssetSpec const &spec, std::vector<std::uint32_t> const &words,
    std::span<kira::Properties const> textures = {}
) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(kira::Properties{}));
    auto const unused =
        context->create<flux::ImageTexture>(constantTexture({0.0F, 0.0F, 0.0F, 1.0F}));
    std::vector<flux::Ref<flux::ImageTexture const>> shaderTextures;
    for (auto const &properties : textures)
        shaderTextures.push_back(context->create<flux::ImageTexture>(properties));
    auto mesh = context->create<flux::TriangleMesh>(largeTriangleData());
    std::vector<flux::Ref<flux::ImageTexture const>> const blackTextures(
        shaderTextures.size(), unused
    );
    auto const createHidden = [&] {
        (void)context->create<flux::ShaderBSDF>(spec, words, blackTextures);
    };
    createHidden();
    auto bsdf = context->create<flux::ShaderBSDF>(spec, words, shaderTextures);
    createHidden();
    kira::Properties primitive;
    primitive.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    primitive.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    (void)context->create<flux::Primitive>(primitive);
    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{0.0F, 0.0F, 1.0F});
    cameraProperties.set("look_at", flux::Vec3f{0.0F, 0.0F, 0.0F});
    cameraProperties.set("fov", 60.0F);
    auto const camera = flux::Camera::create(cameraProperties);

    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    flux::OptixAccumulationBuffer buffer(
        handler, flux::FilmSpec{resolution, flux::FilmChannels::Color}
    );
    handler.render(*camera, buffer, samples);
    flux::Film film{buffer.getSpec()};
    buffer.download(film);
    std::array<double, 3> sum{};
    for (auto const &pixel : film.getChannel<flux::ColorChannel>())
        for (int axis = 0; axis < 3; ++axis)
            sum[axis] += pixel[axis];
    auto const count = static_cast<double>(resolution.x() * resolution.y());
    return {
        static_cast<float>(sum[0] / count), static_cast<float>(sum[1] / count),
        static_cast<float>(sum[2] / count)
    };
}
} // namespace

TEST(OptixShaderTests, ASmoothMaterialXDiffuseReflectsTheEnvironmentTimesItsColor) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto const instance = generateSmoothDiffuse();
    auto const mean = renderMean(instance.spec, instance.words);

    // A pixel of 16 samples has a standard deviation of 6.5% of the color. The mean of 256 pixels
    // has 0.41%, and the tolerance is 3 of that.
    constexpr std::array color{0.8F, 0.2F, 0.2F};
    for (int axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(mean[axis], color[axis], tolerance * color[axis]) << axis;
}

TEST(OptixShaderTests, ASampledTextureColorsTheDiffuseReflection) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    constexpr auto source = R"slang(
import flux_bsdf;
import flux_context;

public IBSDF makeBSDF(uint* words, ShadingContext ctx)
{
    return makeOrenNayarDiffuseBSDF(
        fluxSampleTexture(ctx, 1, float2(0.5)).rgb, 0,
        fluxToShadingFrame(ctx, fluxGetWorldNormal(ctx)));
}
)slang";
    constexpr std::array color{0.8F, 0.5F, 0.2F};
    auto const textures = std::array{
        constantTexture({0.0F, 0.0F, 0.0F, 1.0F}),
        constantTexture({color[0], color[1], color[2], 1.0F}),
    };

    auto const mean = renderMean({.source = source}, {}, textures);

    for (int axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(mean[axis], color[axis], tolerance * color[axis]) << axis;
}

TEST(OptixShaderTests, TheTextureCoordinatesVaryOverTheSurface) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    constexpr auto source = R"slang(
import flux_bsdf;
import flux_context;

public IBSDF makeBSDF(uint* words, ShadingContext ctx)
{
    let uv = fluxGetTexCoord(ctx);
    return makeOrenNayarDiffuseBSDF(
        float3(uv.x, uv.x * uv.x, uv.y), 0, fluxToShadingFrame(ctx, fluxGetWorldNormal(ctx)));
}
)slang";

    auto const mean = renderMean({.source = source}, {});

    // The view covers x and y uniformly in [-h, h], with h = tan(30 degrees) and h^2 = 1/3. So u =
    // 0.5 + x / 2 has the mean 0.5 and the mean square 0.25 + h^2 / 12, and v = 0.5 + y / 4 has the
    // mean 0.5. A constant u cannot match both the mean and the mean square of u. The color varies
    // over the image, which adds about 1% of noise to the mean square, and the tolerance is 3%.
    constexpr std::array expected{0.5F, 0.25F + 1.0F / 36.0F, 0.5F};
    for (int axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(mean[axis], expected[axis], 0.03F * expected[axis]) << axis;
}
