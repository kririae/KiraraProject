#include <fmt/format.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "flux/Mtlx/MtlxBSDFGenerator.h"
#include "flux/Slang/SlangCompiler.h"
#include "kira/Anyhow.h"

namespace {
/// \brief A material whose two diffuse lobes mix.
constexpr auto mixedDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF">
    <input name="color" type="color3" value="0.8, 0.2, 0.2" />
    <input name="roughness" type="float" value="0.3" />
  </oren_nayar_diffuse_bsdf>
  <oren_nayar_diffuse_bsdf name="bg" type="BSDF">
    <input name="color" type="color3" value="0.2, 0.2, 0.8" />
    <input name="roughness" type="float" value="0.5" />
  </oren_nayar_diffuse_bsdf>
  <mix name="m" type="BSDF">
    <input name="fg" type="BSDF" nodename="fg" />
    <input name="bg" type="BSDF" nodename="bg" />
    <input name="mix" type="float" value="0.25" />
  </mix>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="m" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief The same material, with the foreground of the mix left unconnected.
constexpr auto unconnectedDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <oren_nayar_diffuse_bsdf name="bg" type="BSDF">
    <input name="color" type="color3" value="0.2, 0.2, 0.8" />
  </oren_nayar_diffuse_bsdf>
  <mix name="m" type="BSDF">
    <input name="bg" type="BSDF" nodename="bg" />
    <input name="mix" type="float" value="0.25" />
  </mix>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="m" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief A material whose color is the image \p file.
[[nodiscard]] std::string imageDocument(std::string const &file) {
    return fmt::format(
        R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <image name="tex" type="color3">
    <input name="file" type="filename" value="{}" />
  </image>
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF">
    <input name="color" type="color3" nodename="tex" />
  </oren_nayar_diffuse_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="fg" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx",
        file
    );
}

/// \brief A material whose image gets its file from a constant, which MaterialX elides.
constexpr auto constantFileDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <constant name="path" type="filename">
    <input name="value" type="filename" value="a.png" />
  </constant>
  <image name="tex" type="color3">
    <input name="file" type="filename" nodename="path" />
  </image>
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF">
    <input name="color" type="color3" nodename="tex" />
  </oren_nayar_diffuse_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="fg" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief A material whose roughness is a float image of an sRGB file.
constexpr auto dataImageDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <image name="tex" type="float">
    <input name="file" type="filename" value="a.png" colorspace="srgb_texture" />
  </image>
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF">
    <input name="roughness" type="float" nodename="tex" />
  </oren_nayar_diffuse_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="fg" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief A material whose image node sets every input that a texture reads.
constexpr auto imageSettingsDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <image name="tex" type="color3">
    <input name="file" type="filename" value="a.png" colorspace="srgb_texture" />
    <input name="uaddressmode" type="string" value="clamp" />
    <input name="vaddressmode" type="string" value="mirror" />
    <input name="filtertype" type="string" value="closest" />
    <input name="default" type="color3" value="0.25, 0.5, 0.75" />
  </image>
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF">
    <input name="color" type="color3" nodename="tex" />
  </oren_nayar_diffuse_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="fg" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief The part of a backend that the generated source needs.
constexpr auto contextSource = R"slang(
public struct ShadingContext
{
    public float3 normal;
    public float2 uv;
};

public float3 fluxGetWorldNormal(ShadingContext ctx) { return ctx.normal; }

public float2 fluxGetTexCoord(ShadingContext ctx) { return ctx.uv; }

public float4 fluxSampleTexture(ShadingContext ctx, uint slot, float2 uv)
{
    return float4(uv, float(slot), 1.0);
}

public float3 fluxToShadingFrame(ShadingContext ctx, float3 v)
{
    let n = ctx.normal;
    let t = abs(n.z) < 0.999 ? normalize(cross(float3(0.0, 0.0, 1.0), n)) : float3(1.0, 0.0, 0.0);
    return float3(dot(v, t), dot(v, cross(n, t)), dot(v, n));
}
)slang";

/// \brief The entry of a backend, which calls the generated \c makeBSDF.
constexpr auto entrySource = R"slang(
import flux_bsdf;
import flux_context;
import material;

[CudaDeviceExport]
public BSDFResult entry(
    uint* words, ShadingContext ctx, float3 wo, float3 wi, bool eval, float u1, float2 u2)
{
    return execute(makeBSDF(words, ctx), wo, wi, eval, u1, u2);
}
)slang";

/// \brief A material that reads a vertex color, which a rasterization pipeline provides.
constexpr auto unsupportedDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <geomcolor name="vertexColor" type="color3" />
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF">
    <input name="color" type="color3" nodename="vertexColor" />
  </oren_nayar_diffuse_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="fg" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief A material whose color comes from a node that a nodegraph defines, and the nodegraph
/// reads the texture coordinate and an image.
constexpr auto graphDefinedImageDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <nodedef name="ND_textured" node="textured" nodegroup="material">
    <output name="out" type="color3" />
  </nodedef>
  <nodegraph name="NG_textured" nodedef="ND_textured">
    <texcoord name="tc" type="vector2" />
    <image name="tex" type="color3">
      <input name="file" type="filename" value="a.png" />
      <input name="texcoord" type="vector2" nodename="tc" />
    </image>
    <output name="out" type="color3" nodename="tex" />
  </nodegraph>
  <textured name="color" type="color3" />
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF">
    <input name="color" type="color3" nodename="color" />
  </oren_nayar_diffuse_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="fg" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief A material whose roughness comes from a node that \c genslang implements, and whose color
/// comes from a node that a nodegraph implements.
constexpr auto inheritedDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <multiply name="roughness" type="float">
    <input name="in1" type="float" value="0.5" />
    <input name="in2" type="float" value="0.6" />
  </multiply>
  <contrast name="color" type="color3">
    <input name="in" type="color3" value="0.8, 0.2, 0.2" />
    <input name="amount" type="float" value="1.5" />
  </contrast>
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF">
    <input name="color" type="color3" nodename="color" />
    <input name="roughness" type="float" nodename="roughness" />
  </oren_nayar_diffuse_bsdf>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="fg" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

/// \brief A material whose mix input has a type that its definition does not hold.
constexpr auto malformedDocument = R"mtlx(<?xml version="1.0"?>
<materialx version="1.39">
  <oren_nayar_diffuse_bsdf name="fg" type="BSDF" />
  <oren_nayar_diffuse_bsdf name="bg" type="BSDF" />
  <mix name="m" type="BSDF">
    <input name="fg" type="BSDF" nodename="fg" />
    <input name="bg" type="BSDF" nodename="bg" />
    <input name="mix" type="vector2" />
  </mix>
  <surface name="s" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="m" />
  </surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="s" />
  </surfacematerial>
</materialx>)mtlx";

[[nodiscard]] flux::MtlxBSDFGenerator::GenerationResult
tryGenerate(std::string const &document, std::vector<std::string> const &interactive = {}) {
    auto const generator = flux::MtlxBSDFGenerator{FLUX_MATERIALX_DIR};
    return generator.generate(document, "M", {}, interactive);
}

[[nodiscard]] flux::MtlxBSDFGenerator::Instance
generate(std::string const &document, std::vector<std::string> const &interactive = {}) {
    auto result = tryGenerate(document, interactive);
    if (!result.instance)
        throw kira::Anyhow{"the material generated nothing"};
    return *std::move(result.instance);
}

[[nodiscard]] std::string readLibrary() {
    auto const path = std::string{FLUX_SLANG_DIR} + "/flux-bsdf.slang";
    auto stream = std::ifstream{path};
    if (!stream)
        throw kira::Anyhow{"cannot read {}", path};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string compile(flux::ShaderAssetSpec const &spec) {
    auto const compiler = flux::SlangCompiler{flux::SlangCompiler::Target::CudaSource};
    auto const modules = std::vector{
        flux::SlangModule{.name = "flux_bsdf", .source = readLibrary()},
        flux::SlangModule{.name = "flux_context", .source = contextSource},
        flux::SlangModule{.name = "material", .source = spec.source},
        flux::SlangModule{.name = "entry", .source = entrySource},
    };
    return compiler.compile(modules, "entry");
}

void expectOneConcreteType(flux::MtlxBSDFGenerator::Instance const &instance) {
    auto const code = compile(instance.spec);

    EXPECT_EQ(code.find("AnyValue"), std::string::npos);
}
} // namespace

TEST(MtlxGeneratorTests, OnlyTheInteractiveInputsBecomeWordsHoldingTheirValues) {
    auto const instance = generate(mixedDocument, {"fg/color", "m/mix"});

    auto values = std::vector<float>{};
    for (auto const word : instance.words)
        values.push_back(std::bit_cast<float>(word));
    std::ranges::sort(values);
    EXPECT_EQ(values, (std::vector<float>{0.2F, 0.2F, 0.25F, 0.8F}));
}

TEST(MtlxGeneratorTests, CompilesAMixedMaterialToOneConcreteType) {
    expectOneConcreteType(generate(mixedDocument, {"fg/color"}));
}

TEST(MtlxGeneratorTests, CompilesAnUnconnectedClosureInputToOneConcreteType) {
    expectOneConcreteType(generate(unconnectedDocument));
}

TEST(MtlxGeneratorTests, CompilesTheNodesThatTheTargetInherits) {
    expectOneConcreteType(generate(inheritedDocument));
}

TEST(MtlxGeneratorTests, ANodegraphThatDefinesANodeReadsTheContextAndTheTextures) {
    auto const instance = generate(graphDefinedImageDocument);

    ASSERT_EQ(instance.textures.size(), 1U);
    EXPECT_EQ(instance.textures[0].image.path, "a.png");
    expectOneConcreteType(instance);
}

TEST(MtlxGeneratorTests, GeneratesNothingForANodeThatGenfluxCannotRun) {
    auto const result = tryGenerate(unsupportedDocument);

    EXPECT_FALSE(result.instance);
    EXPECT_EQ(result.diagnostics.size(), 1U);
}

TEST(MtlxGeneratorTests, GeneratesNothingForADocumentThatMaterialXCannotBuild) {
    auto const result = tryGenerate(malformedDocument);

    EXPECT_FALSE(result.instance);
    EXPECT_EQ(result.diagnostics.size(), 1U);
}

TEST(MtlxGeneratorTests, GeneratesNothingForADocumentThatDoesNotParse) {
    auto const result = tryGenerate("<materialx");

    EXPECT_FALSE(result.instance);
    EXPECT_EQ(result.diagnostics.size(), 1U);
}

TEST(MtlxGeneratorTests, ReportsAnInteractivePathThatNoInputMatches) {
    auto const result = tryGenerate(mixedDocument, {"fg/color", "nothing/here"});

    EXPECT_TRUE(result.instance);
    EXPECT_EQ(result.diagnostics.size(), 1U);
}

TEST(MtlxGeneratorTests, AnImageFileChangesTheTexturesAndNotTheSource) {
    auto const first = generate(imageDocument("a.png"));
    auto const second = generate(imageDocument("b.png"));

    EXPECT_EQ(first.spec.source, second.spec.source);
    ASSERT_EQ(first.textures.size(), 1U);
    ASSERT_EQ(second.textures.size(), 1U);
    EXPECT_EQ(first.textures[0].image.path, "a.png");
    EXPECT_EQ(second.textures[0].image.path, "b.png");
    expectOneConcreteType(first);
}

TEST(MtlxGeneratorTests, TheNameOfTheMaterialDoesNotChangeTheSource) {
    auto const generator = flux::MtlxBSDFGenerator{FLUX_MATERIALX_DIR};
    auto renamed = std::string{mixedDocument};
    renamed.replace(renamed.find(R"(name="M")"), 8, R"(name="Other")");

    auto const first = generator.generate(mixedDocument, "M", {});
    auto const second = generator.generate(renamed, "Other", {});

    ASSERT_TRUE(first.instance);
    ASSERT_TRUE(second.instance);
    EXPECT_EQ(first.instance->spec.source, second.instance->spec.source);
}

TEST(MtlxGeneratorTests, AnImageNodeSetsTheSpecOfItsTexture) {
    auto const instance = generate(imageSettingsDocument);

    ASSERT_EQ(instance.textures.size(), 1U);
    EXPECT_EQ(
        instance.textures[0],
        (flux::ImageTextureSpec{
            .image = {.path = "a.png", .colorSpace = std::string{flux::srgbTexture}},
            .addressModes =
                {flux::ImageTextureAddressMode::Clamp, flux::ImageTextureAddressMode::Mirror},
            .filterMode = flux::ImageTextureFilterMode::Point,
            .fallback = {0.25F, 0.5F, 0.75F, 1.0F},
        })
    );
}

TEST(MtlxGeneratorTests, AConstantFeedsTheFileOfAnImage) {
    auto const instance = generate(constantFileDocument);

    ASSERT_EQ(instance.textures.size(), 1U);
    EXPECT_EQ(instance.textures[0].image.path, "a.png");
    expectOneConcreteType(instance);
}

TEST(MtlxGeneratorTests, AFloatImageIsLinearWhateverItsFileSays) {
    auto const instance = generate(dataImageDocument);

    ASSERT_EQ(instance.textures.size(), 1U);
    EXPECT_EQ(instance.textures[0].image.colorSpace, flux::linearRec709);
}
