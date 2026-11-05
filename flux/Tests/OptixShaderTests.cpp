#include <MaterialXCore/Document.h> // NOLINT(llvm-include-order)
#include <MaterialXFormat/XmlIo.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

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
    auto instance = generator.generate(document, "M", interactive);
    if (!instance)
        throw kira::Anyhow{"the material generated nothing"};
    return *std::move(instance);
}

/// \brief A triangle that fills the view of \c renderMean's camera.
[[nodiscard]] flux::TriangleMesh::Data largeTriangleData() {
    return {
        .vertices = flux::test::sharedBuffer(
            flux::Vec3f{-10.0F, -10.0F, 0.0F}, flux::Vec3f{10.0F, -10.0F, 0.0F},
            flux::Vec3f{0.0F, 10.0F, 0.0F}
        ),
        .triangles = flux::test::sharedBuffer(flux::Vec3u{0, 1, 2}),
    };
}

/// \brief Renders the triangle with the shader of \p instance under a constant environment, and
/// returns the mean of the pixels.
[[nodiscard]] flux::Spectrum renderMean(flux::MtlxBSDFGenerator::Instance const &instance) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(kira::Properties{}));
    auto mesh = context->create<flux::TriangleMesh>(largeTriangleData());
    auto bsdf = context->create<flux::ShaderBSDF>(instance.spec, instance.words);
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

    auto const mean = renderMean(generateSmoothDiffuse());

    // A pixel of 16 samples has a standard deviation of 6.5% of the color. The mean of 256 pixels
    // has 0.41%, and the tolerance is 3 of that.
    constexpr std::array color{0.8F, 0.2F, 0.2F};
    for (int axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(mean[axis], color[axis], tolerance * color[axis]) << axis;
}
