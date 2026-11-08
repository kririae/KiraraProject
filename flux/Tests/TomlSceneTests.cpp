#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "flux/FLux/FLuxCLI.h"
#include "flux/FLux/TomlScene.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

namespace {
/// Writes a scene with \p filmProperties in its film table and \p tables after it, and returns its
/// path.
[[nodiscard]] std::filesystem::path
writeScene(std::string const &filmProperties, std::string const &tables = "") {
    auto const path = std::filesystem::path{testing::TempDir()} / "flux-cli-scene.toml";
    std::ofstream output(path);
    output << "[camera]\n[integrator]\n[film]\nresolution = [4, 2]\n" << filmProperties << tables;
    return path;
}

/// Writes the MaterialX document \p document as \p name beside the scene files, and returns its
/// path.
[[nodiscard]] std::filesystem::path
writeDocument(std::string const &name, std::string const &document) {
    auto const path = std::filesystem::path{testing::TempDir()} / name;
    std::ofstream output(path);
    output << document;
    return path;
}

/// A material M with a diffuse BSDF and no EDF node.
constexpr auto quietDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <oren_nayar_diffuse_bsdf name="b" type="BSDF">
    <input name="roughness" type="float" value="0" />
  </oren_nayar_diffuse_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="b" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// A material M that emits a color.
constexpr auto emittingDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <uniform_edf name="e" type="EDF">
    <input name="color" type="color3" value="1, 1, 1" />
  </uniform_edf>
  <surface name="s" type="surfaceshader">
    <input name="edf" type="EDF" nodename="e" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";
} // namespace

TEST(TomlSceneTests, LoadsTheFilmOfAScene) {
    auto options = flux::FluxCLIOptions{.scenePath = writeScene("num_samples = 16\n")};
    auto scene = flux::loadTomlScene(options);

    EXPECT_EQ(scene.film.resolution, (flux::Vec2u{4, 2}));
    EXPECT_EQ(scene.film.channels, flux::FilmChannels::Color);
    EXPECT_EQ(scene.samplesPerPixel, 16);

    options.samplesPerPixel = 4;
    EXPECT_EQ(flux::loadTomlScene(options).samplesPerPixel, 4);
}

TEST(TomlSceneTests, RendersOneSampleByDefault) {
    auto const options = flux::FluxCLIOptions{.scenePath = writeScene("")};
    EXPECT_EQ(flux::loadTomlScene(options).samplesPerPixel, 1);
}

TEST(TomlSceneTests, KeepsZeroSamples) {
    auto options = flux::FluxCLIOptions{.scenePath = writeScene("num_samples = 0\n")};
    EXPECT_EQ(flux::loadTomlScene(options).samplesPerPixel, 0);

    options.scenePath = writeScene("num_samples = 8\n");
    options.samplesPerPixel = 0;
    EXPECT_EQ(flux::loadTomlScene(options).samplesPerPixel, 0);
}

TEST(TomlSceneTests, BindsTheShaderBSDFAndEDFOfAMaterialToItsPrimitive) {
    struct Row {
        char const *name;
        char const *document;
        std::string primitiveProperties;
        bool throws;
        bool mayEmit;
    };
    auto const light = std::string{"light = { emission = [1.0, 1.0, 1.0] }\n"};
    auto const rows = std::vector<Row>{
        {"a material that emits", emittingDocument, "material = \"m\"\n", false, true},
        {"a material that emits nothing", quietDocument, "material = \"m\"\n", false, false},
        {"a material and a BSDF", quietDocument, "material = \"m\"\nbsdf = \"b\"\n", true, false},
        {"a material and a light", quietDocument, "material = \"m\"\n" + light, true, false},
    };

    auto const geometry = std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Cbox/geometry/light.ply";
    for (auto const &row : rows) {
        SCOPED_TRACE(row.name);
        auto const document = writeDocument("flux-cli-material.mtlx", row.document);
        auto const options = flux::FluxCLIOptions{
            .scenePath = writeScene(
                "", "[[bsdf]]\nname = \"b\"\ntype = \"diffuse\"\n"
                    "[[material]]\nname = \"m\"\ntype = \"materialx\"\nfile = \"" +
                        document.string() +
                        "\"\nmaterial = \"M\"\n[[primitive]]\ntype = \"trimesh\"\npath = \"" +
                        geometry.string() + "\"\n" + row.primitiveProperties
            )
        };
        if (row.throws) {
            EXPECT_THROW((void)flux::loadTomlScene(options), kira::Anyhow);
            continue;
        }

        auto const scene = flux::loadTomlScene(options);
        auto const primitives = scene.context->getObjects<flux::Primitive>();
        ASSERT_EQ(primitives.size(), 1U);
        auto const bsdf = primitives[0]->getBSDF();
        auto const edf = primitives[0]->getEDF();
        ASSERT_NE(bsdf, nullptr);
        ASSERT_NE(edf, nullptr);
        ASSERT_EQ(bsdf->getType(), flux::BSDFType::Shader);
        ASSERT_EQ(edf->getType(), flux::EDFType::Shader);
        auto const &shaderEDF = static_cast<flux::ShaderEDF const &>(*edf);
        EXPECT_EQ(shaderEDF.mayEmit(), row.mayEmit);
        EXPECT_EQ(static_cast<flux::ShaderBSDF const &>(*bsdf).getAsset(), shaderEDF.getAsset());
    }
}
