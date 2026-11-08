#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "SceneUtils.h"
#include "TestUtils.h"
#include "flux/Core/MathUtils.h"
#include "flux/Mtlx/MtlxShaderGenerator.h"
#include "flux/Optix/OptixAccumulationBuffer.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
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

/// \brief A Lambertian reflector, the leaf of the hand-written Slang materials.
constexpr auto lambertianLeaf = R"slang(
import flux_material;

public struct LambertianBSDF : IBSDF
{
    public float3 albedo;
    public float3 n;

    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let nf = dot(n, wo) < 0.0 ? -n : n;
        let nDotL = dot(nf, wi);
        if (nDotL <= 0.0)
            return {float3(0.0), 0.0};
        return {albedo * nDotL / 3.14159265, cosineHemispherePdf(nDotL)};
    }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        let nf = dot(n, wo) < 0.0 ? -n : n;
        return {sampleCosineHemisphere(nf, u2), 1.0, BSDFLobe.DiffuseReflection};
    }

    public float3 throughput(float3 wo) { return float3(0.0); }
};

public LambertianBSDF makeLambertianBSDF(float3 albedo, float3 n) { return {albedo, n}; }
)slang";

/// \brief Emission of the color \p color in every direction.
[[nodiscard]] std::string uniformEmissionDocument(std::string const &color) {
    return fmt::format(
        R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <uniform_edf name="e" type="EDF">
    <input name="color" type="color3" value="{}" />
  </uniform_edf>
  <surface name="s" type="surfaceshader">
    <input name="edf" type="EDF" nodename="e" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx",
        color
    );
}

/// \brief A uniform emission that a Schlick factor scales by \c color0 at normal incidence.
constexpr auto schlickEmissionDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <uniform_edf name="e" type="EDF">
    <input name="color" type="color3" value="0.5, 1, 2" />
  </uniform_edf>
  <generalized_schlick_edf name="rim" type="EDF">
    <input name="color0" type="color3" value="0.5, 0.25, 1" />
    <input name="color90" type="color3" value="1, 1, 1" />
    <input name="base" type="EDF" nodename="e" />
  </generalized_schlick_edf>
  <surface name="s" type="surfaceshader">
    <input name="edf" type="EDF" nodename="rim" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief A conductor whose Fresnel reflectance is 1 at every angle.
constexpr auto fullReflectanceConductorDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <conductor_bsdf name="fg" type="BSDF">
    <input name="ior" type="color3" value="0, 0, 0" />
    <input name="extinction" type="color3" value="100, 100, 100" />
    <input name="roughness" type="vector2" value="0.6, 0.6" />
  </conductor_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="fg" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief The reflector, with its color read from the words.
[[nodiscard]] flux::ShaderInstance generateSmoothDiffuse() {
    auto const generator = flux::MtlxShaderGenerator{FLUX_MATERIALX_DIR};
    auto const interactive = std::array<std::string, 1>{"fg/color"};
    auto result = generator.generate(smoothDocument, "M", {}, interactive);
    if (!result.instance)
        throw kira::Anyhow{"the generator produced no shader"};
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

/// \brief A diffuse reflector whose color is the world-space tangent.
constexpr auto tangentDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <tangent name="t" type="vector3">
    <input name="space" type="string" value="world" />
  </tangent>
  <convert name="c" type="color3">
    <input name="in" type="vector3" nodename="t" />
  </convert>
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF">
    <input name="color" type="color3" nodename="c" />
    <input name="roughness" type="float" value="0" />
  </oren_nayar_diffuse_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="fg" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

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
/// environment, and returns the mean of the pixels. Each pixel takes \p sampleCount samples.
///
/// The shader samples \p textures. An image texture that nothing samples precedes them, so that
/// the image-texture index of a texture is not its slot. A shader BSDF of the same source, which no
/// primitive uses, binds black textures before and after the visible one.
///
/// If \p edit is set, the mean is that of a second render, after \p edit edits the BSDF.
[[nodiscard]] flux::Spectrum renderMean(
    flux::ShaderAssetSpec const &spec, std::vector<std::uint32_t> const &words,
    std::span<kira::Properties const> textures = {},
    std::function<void(flux::ShaderBSDF &)> const &edit = {},
    flux::TriangleMesh::Data triangleData = largeTriangleData(), std::uint32_t sampleCount = samples
) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(kira::Properties{}));
    auto const unused =
        context->create<flux::ImageTexture>(constantTexture({0.0F, 0.0F, 0.0F, 1.0F}));
    std::vector<flux::Ref<flux::ImageTexture const>> shaderTextures;
    for (auto const &properties : textures)
        shaderTextures.push_back(context->create<flux::ImageTexture>(properties));
    auto mesh = context->create<flux::TriangleMesh>(std::move(triangleData));
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
    auto const render = [&] {
        handler.render(*camera, buffer, sampleCount);
        flux::Film film{buffer.getSpec()};
        buffer.download(film);
        std::array<double, 3> sum{};
        for (auto const &pixel : film.getChannel<flux::ColorChannel>())
            for (int axis = 0; axis < 3; ++axis)
                sum[axis] += pixel[axis];
        auto const count = static_cast<double>(resolution.x() * resolution.y());
        return flux::Spectrum{
            static_cast<float>(sum[0] / count), static_cast<float>(sum[1] / count),
            static_cast<float>(sum[2] / count)
        };
    };
    auto mean = render();
    if (edit) {
        edit(*bsdf);
        handler.sync();
        buffer.clear();
        mean = render();
    }
    return mean;
}

/// \brief Renders \p context through \p camera, and returns the mean of the pixels. Each pixel
/// takes \p sampleCount samples.
[[nodiscard]] flux::Spectrum renderContextMean(
    flux::Ref<flux::Context> const &context, flux::Camera const &camera, std::uint32_t sampleCount
) {
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    flux::OptixAccumulationBuffer buffer(
        handler, flux::FilmSpec{resolution, flux::FilmChannels::Color}
    );
    handler.render(camera, buffer, sampleCount);
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

/// \brief The rotation about x by 60 degrees.
constexpr std::array<float, 12> tiltTransform{1.0F,        0.0F, 0.0F, 0.0F,       0.0F, 0.5F,
                                              -0.8660254F, 0.0F, 0.0F, 0.8660254F, 0.5F, 0.0F};

/// \brief Renders the triangle, tilted by \c tiltTransform, with the BSDF and the EDF of the shader
/// of \p document, along its normal from the front (\p side 1) or the back (\p side -1), with the
/// field of view \p fov, and returns the mean of the pixels.
///
/// The scene has no environment and the path ends at the first hit, so every pixel is the emission
/// that the camera ray finds there.
[[nodiscard]] flux::Spectrum
renderEmissionMean(std::string const &document, float side, float fov) {
    auto const generator = flux::MtlxShaderGenerator{FLUX_MATERIALX_DIR};
    auto const generated = generator.generate(document, "M", {});
    if (!generated.instance)
        throw kira::Anyhow{"the generator produced no shader"};
    auto const &instance = *generated.instance;

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    kira::Properties integratorProperties;
    integratorProperties.set("max_depth", 1U);
    context->setActiveIntegrator(context->create<flux::PathIntegrator>(integratorProperties));
    auto const mesh = context->create<flux::TriangleMesh>(largeTriangleData());
    auto const bsdf = context->create<flux::ShaderBSDF>(instance.spec, instance.words);
    auto const edf = context->create<flux::ShaderEDF>(instance.spec, instance.words);
    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    properties.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    properties.set("edf_ctx_id", static_cast<std::int64_t>(edf->getContextId()));
    auto const primitive = context->create<flux::Primitive>(properties);
    primitive->setTransform(tiltTransform);
    kira::Properties cameraProperties;
    cameraProperties.set(
        "position", flux::transformPoint(tiltTransform.data(), {0.0F, 0.0F, 3.0F * side})
    );
    cameraProperties.set("look_at", flux::Vec3f{0.0F, 0.0F, 0.0F});
    cameraProperties.set("fov", fov);
    auto const camera = flux::Camera::create(cameraProperties);

    return renderContextMean(context, *camera, 1);
}

constexpr std::array<float, 12> identityTransform{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
                                                  0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F};

/// \brief The transform of an emitter above the floor of \c renderLitFloorMean: a scale by
/// \p scale, a tilt by 25 degrees about x and 20 degrees about y, and a move to (0, 1, 2), so that
/// no geometry-space direction or position is a world-space one.
[[nodiscard]] std::array<float, 12> emitterTransform(flux::Vec3f const &scale) {
    auto const [sinX, cosX] = std::pair{std::sin(-0.4363323F), std::cos(-0.4363323F)};
    auto const [sinY, cosY] = std::pair{std::sin(0.3490659F), std::cos(0.3490659F)};
    // The rotation about y of the rotation about x, after the scale.
    return {
        scale.x() * cosY,
        scale.y() * sinY * sinX,
        scale.z() * sinY * cosX,
        0.0F,
        0.0F,
        scale.y() * cosX,
        -scale.z() * sinX,
        1.0F,
        -scale.x() * sinY,
        scale.y() * cosY * sinX,
        scale.z() * cosY * cosX,
        2.0F,
    };
}

/// \brief The rectangle of the local plane z = 0 with x in [\p left, \p right] and y in
/// [-0.5, 0.5], facing -z, with the texture coordinates (x + 0.5, 0.5), moved by \p transform.
[[nodiscard]] flux::TriangleMesh::Data emitterRectangleData(
    float left, float right, std::array<float, 12> const &transform = identityTransform
) {
    auto const corner = [&](float x, float y) {
        return flux::transformPoint(transform.data(), {x, y, 0.0F});
    };
    return {
        .vertices = flux::test::sharedBuffer(
            corner(left, -0.5F), corner(left, 0.5F), corner(right, 0.5F), corner(right, -0.5F)
        ),
        .triangles = flux::test::sharedBuffer(flux::Vec3u{0, 1, 2}, flux::Vec3u{0, 2, 3}),
        .texCoords = flux::test::sharedBuffer(
            flux::Vec2f{left + 0.5F, 0.5F}, flux::Vec2f{left + 0.5F, 0.5F},
            flux::Vec2f{right + 0.5F, 0.5F}, flux::Vec2f{right + 0.5F, 0.5F}
        ),
    };
}

/// \brief Places an emitter of \p data with \p edf above the floor of \c renderLitFloorMean,
/// under \p transform, which scales by 1.2 by default.
void addEmitter(
    flux::Context &context, flux::TriangleMesh::Data data, flux::Ref<flux::EDF const> const &edf,
    std::array<float, 12> const &transform = emitterTransform({1.2F, 1.2F, 1.2F})
) {
    auto const mesh = context.create<flux::TriangleMesh>(std::move(data));
    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    properties.set("edf_ctx_id", static_cast<std::int64_t>(edf->getContextId()));
    auto const primitive = context.create<flux::Primitive>(properties);
    primitive->setTransform(transform);
}

/// \brief Renders a diffuse floor that the emitters of \p addEmitters light, and returns the mean
/// of the pixels. Each pixel takes \p sampleCount samples.
///
/// A path takes two vertices, so the floor receives the light of an emitter by light sampling at
/// its first vertex and by BSDF sampling that hits the emitter at its second. The camera sees only
/// the floor.
[[nodiscard]] flux::Spectrum renderLitFloorMean(
    std::function<void(flux::Context &)> const &addEmitters, std::uint32_t sampleCount
) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    kira::Properties integratorProperties;
    integratorProperties.set("max_depth", 2U);
    context->setActiveIntegrator(context->create<flux::PathIntegrator>(integratorProperties));

    kira::Properties bsdfProperties;
    bsdfProperties.set("type", "diffuse");
    bsdfProperties.set("R", flux::Spectrum{0.5F});
    auto const bsdf = context->create<flux::BSDF>(bsdfProperties);
    auto const floor = context->create<flux::TriangleMesh>(largeTriangleData());
    kira::Properties floorProperties;
    floorProperties.set("geometry_ctx_id", static_cast<std::int64_t>(floor->getContextId()));
    floorProperties.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    (void)context->create<flux::Primitive>(floorProperties);
    addEmitters(*context);

    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{0.0F, -4.0F, 1.5F});
    cameraProperties.set("look_at", flux::Vec3f{0.0F, -1.0F, 0.0F});
    cameraProperties.set("fov", 25.0F);
    auto const camera = flux::Camera::create(cameraProperties);
    return renderContextMean(context, *camera, sampleCount);
}

[[nodiscard]] flux::Ref<flux::EDF>
makeConstantEDF(flux::Context &context, flux::Spectrum const &radiance) {
    kira::Properties properties;
    properties.set("radiance", radiance);
    return context.create<flux::ConstantEDF>(properties);
}

/// \brief A uniform emission of the color of the image \c TwoTexels.ppm at the texture
/// coordinate, which a closest filter reads without blending its two texels.
constexpr auto twoTexelEmissionDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <image name="tex" type="color3">
    <input name="file" type="filename" value="TwoTexels.ppm" colorspace="lin_rec709" />
    <input name="filtertype" type="string" value="closest" />
    <input name="uaddressmode" type="string" value="clamp" />
    <input name="vaddressmode" type="string" value="clamp" />
  </image>
  <uniform_edf name="e" type="EDF">
    <input name="color" type="color3" nodename="tex" />
  </uniform_edf>
  <surface name="s" type="surfaceshader">
    <input name="edf" type="EDF" nodename="e" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief Creates the shader EDF of \p document, with the textures that its shader samples.
[[nodiscard]] flux::Ref<flux::ShaderEDF>
makeShaderEDF(flux::Context &context, std::string const &document) {
    auto const generator = flux::MtlxShaderGenerator{FLUX_MATERIALX_DIR};
    auto generated = generator.generate(
        document, "M", std::array{std::filesystem::path{FLUX_TEST_FIXTURES_DIR}}
    );
    if (!generated.instance)
        throw kira::Anyhow{"the generator produced no shader"};
    auto &instance = *generated.instance;
    std::vector<flux::Ref<flux::ImageTexture const>> textures;
    for (auto const &spec : instance.textures)
        textures.push_back(context.create<flux::ImageTexture>(spec));
    return context.create<flux::ShaderEDF>(
        instance.spec, std::move(instance.words), std::move(textures)
    );
}
} // namespace

TEST(OptixShaderTests, AShaderEDFEmitsAsItsShaderSaysToBothSidesOfATiltedSurface) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    struct Row {
        char const *name;
        std::string document;
        float side;
        float fov;
        flux::Spectrum expected;
        float tolerance;
    };
    // The surface is tilted, so a world-space direction is no shading-frame direction. A view of 1
    // degree is normal to within 0.5 degrees, where the Schlick factor differs from color0 by at
    // most 1e-10.
    auto const rows = std::vector<Row>{
        {"uniform front",
         uniformEmissionDocument("0.25, 0.5, 2"),
         1.0F,
         60.0F,
         {0.25F, 0.5F, 2.0F},
         1e-5F},
        {"uniform back",
         uniformEmissionDocument("0.25, 0.5, 2"),
         -1.0F,
         60.0F,
         {0.25F, 0.5F, 2.0F},
         1e-5F},
        {"Schlick at normal incidence",
         schlickEmissionDocument,
         1.0F,
         1.0F,
         {0.25F, 0.25F, 2.0F},
         1e-4F},
    };

    for (auto const &row : rows) {
        SCOPED_TRACE(row.name);
        auto const mean = renderEmissionMean(row.document, row.side, row.fov);

        for (int axis = 0; axis < 3; ++axis)
            EXPECT_NEAR(mean[axis], row.expected[axis], row.tolerance * row.expected[axis]) << axis;
    }
}

TEST(OptixShaderTests, AFloorLitByATwoTexelShaderEmitterMatchesOneLitByItsTwoConstantHalves) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    // The texels of TwoTexels.ppm are linear, and their luminances, 0.2126 and 0.2127, differ by
    // less than 0.1%. So the light table gives each of the two constant halves the probability
    // 1/2.
    constexpr auto left = flux::Spectrum{1.0F, 0.0F, 0.0F};
    constexpr auto right = flux::Spectrum{0.0F, 51.0F / 255.0F, 246.0F / 255.0F};
    static_assert(flux::luminance(right) < 1.001F * flux::luminance(left));
    static_assert(flux::luminance(left) < 1.001F * flux::luminance(right));
    constexpr std::uint32_t emitterSamples = 512;
    auto const transform = emitterTransform({2.0F, 1.0F, 0.5F});
    auto const textured = renderLitFloorMean([&](flux::Context &context) {
        addEmitter(
            context, emitterRectangleData(-0.5F, 0.5F),
            makeShaderEDF(context, twoTexelEmissionDocument), transform
        );
    }, emitterSamples);
    auto const halves = renderLitFloorMean([&](flux::Context &context) {
        addEmitter(
            context, emitterRectangleData(-0.5F, 0.0F, transform), makeConstantEDF(context, left),
            identityTransform
        );
        addEmitter(
            context, emitterRectangleData(0.0F, 0.5F, transform), makeConstantEDF(context, right),
            identityTransform
        );
    }, emitterSamples);

    // The renders sample different points, so they differ by noise. One half lights each channel,
    // and a light sample picks it with probability 1/2 in both scenes, with the density 1 / area
    // of the rectangle. So a light sample is zero or twice the geometric term of its point, which
    // has a relative standard deviation over a half of at most 0.43 for the pixels in view. Its
    // relative variance is then at most 1 + 2 * 0.43^2 = 1.37. A path adds a BSDF sample to the
    // light sample, and the power heuristic keeps the second moment of the pair below that of the
    // light sample alone. Over the pixels in view, 256 pixels of 512 samples give the mean a
    // relative standard deviation of at most 0.38% in each render, and the difference of two
    // renders has 0.54%. The tolerance is 5.5 of that. The average of the area scales of the axes
    // is 1.17 where the area scale of this rectangle is 2.
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_GT(halves[axis], 0.0F) << axis;
        EXPECT_NEAR(textured[axis], halves[axis], 0.03F * halves[axis]) << axis;
    }
}

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

TEST(OptixShaderTests, AMaterialXConductorOfFullReflectanceReflectsTheEnvironment) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto const generator = flux::MtlxShaderGenerator{FLUX_MATERIALX_DIR};
    auto const generated = generator.generate(fullReflectanceConductorDocument, "M", {});
    ASSERT_TRUE(generated.instance);

    constexpr std::uint32_t furnaceSamples = 1024;
    auto const mean = renderMean(
        generated.instance->spec, generated.instance->words, {}, {}, largeTriangleData(),
        furnaceSamples
    );

    // MaterialX's fit of the directional albedo leaves the mean 0.5% below 1.
    for (int axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(mean[axis], 1.0F, 0.01F) << axis;
}

TEST(OptixShaderTests, ASampledTextureColorsTheDiffuseReflection) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto const source = std::string{lambertianLeaf} + R"slang(
import flux_material;
import flux_context;

public IBSDF makeBSDF(uint* words, ShadingContext ctx)
{
    return makeLambertianBSDF(
        fluxSampleTexture(ctx, 1, float2(0.5)).rgb, fluxToShadingFrame(ctx, fluxGetWorldNormal(ctx)));
}

public IEDF makeEDF(uint* words, ShadingContext ctx) { return EmptyEDF(); }
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

    auto const source = std::string{lambertianLeaf} + R"slang(
import flux_material;
import flux_context;

public IBSDF makeBSDF(uint* words, ShadingContext ctx)
{
    let uv = fluxGetTexCoord(ctx);
    return makeLambertianBSDF(
        float3(uv.x, uv.x * uv.x, uv.y), fluxToShadingFrame(ctx, fluxGetWorldNormal(ctx)));
}

public IEDF makeEDF(uint* words, ShadingContext ctx) { return EmptyEDF(); }
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

TEST(OptixShaderTests, ReplacingTheShaderOfABSDFRendersTheNewShader) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto const source = std::string{lambertianLeaf} + R"slang(
import flux_material;
import flux_context;

public IBSDF makeBSDF(uint* words, ShadingContext ctx)
{
    return makeLambertianBSDF(
        float3(0.2, 0.5, 0.8), fluxToShadingFrame(ctx, fluxGetWorldNormal(ctx)));
}

public IEDF makeEDF(uint* words, ShadingContext ctx) { return EmptyEDF(); }
)slang";
    constexpr std::array color{0.2F, 0.5F, 0.8F};
    auto const instance = generateSmoothDiffuse();

    auto const mean = renderMean(instance.spec, instance.words, {}, [&](flux::ShaderBSDF &bsdf) {
        bsdf.setShader({.source = source}, {}, {});
    });

    for (int axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(mean[axis], color[axis], tolerance * color[axis]) << axis;
}

TEST(OptixShaderTests, TheTangentNodeIsTheDirectionInWhichUGrows) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto const generator = flux::MtlxShaderGenerator{FLUX_MATERIALX_DIR};
    auto const generated = generator.generate(tangentDocument, "M", {});
    ASSERT_TRUE(generated.instance);
    // u grows along y, where the tangent of the frame of the normal (0, 0, 1) points along x.
    auto triangleData = largeTriangleData();
    triangleData.texCoords = flux::test::sharedBuffer(
        flux::Vec2f{-4.5F, -2.0F}, flux::Vec2f{-4.5F, 3.0F}, flux::Vec2f{5.5F, 0.5F}
    );

    auto const mean = renderMean(
        generated.instance->spec, generated.instance->words, {}, {}, std::move(triangleData)
    );

    constexpr std::array color{0.0F, 1.0F, 0.0F};
    for (int axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(mean[axis], color[axis], tolerance) << axis;
}
