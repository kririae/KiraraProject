#include <MaterialXCore/Document.h> // NOLINT(llvm-include-order)
#include <MaterialXFormat/XmlIo.h>
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <fstream>
#include <optional>
#include <ranges>
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

/// \brief The part of a backend that the generated source needs.
constexpr auto contextSource = R"slang(
public struct ShadingContext
{
    public float3 normal;
};

public float3 fluxGetWorldNormal(ShadingContext ctx) { return ctx.normal; }

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

[[nodiscard]] std::optional<flux::MtlxBSDFGenerator::Instance>
tryGenerate(std::string const &document, std::vector<std::string> const &interactive = {}) {
    auto const generator = flux::MtlxBSDFGenerator{FLUX_MATERIALX_DIR};
    auto parsed = MaterialX::createDocument();
    MaterialX::readFromXmlString(parsed, document);
    return generator.generate(parsed, "M", interactive);
}

[[nodiscard]] flux::MtlxBSDFGenerator::Instance
generate(std::string const &document, std::vector<std::string> const &interactive = {}) {
    auto instance = tryGenerate(document, interactive);
    if (!instance)
        throw kira::Anyhow{"the material generated nothing"};
    return *std::move(instance);
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

TEST(MtlxGeneratorTests, OnlyTheInteractiveInputsBecomeParametersWithTheirValuesAsWords) {
    auto const instance = generate(mixedDocument, {"fg/color", "m/mix"});

    ASSERT_EQ(instance.spec.parameters.size(), 2U);
    EXPECT_EQ(instance.words.size(), 4U);
    for (auto const &parameter : instance.spec.parameters) {
        auto const expected = parameter.path == "fg/color" ? std::vector<float>{0.8F, 0.2F, 0.2F}
                                                           : std::vector<float>{0.25F};
        ASSERT_EQ(parameter.wordCount, expected.size()) << parameter.path;
        for (auto const index : std::views::iota(std::size_t{}, expected.size()))
            EXPECT_EQ(
                std::bit_cast<float>(instance.words[parameter.offset + index]), expected[index]
            ) << parameter.path;
    }
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

TEST(MtlxGeneratorTests, GeneratesNothingForANodeThatGenfluxCannotRun) {
    EXPECT_FALSE(tryGenerate(unsupportedDocument));
}

TEST(MtlxGeneratorTests, GeneratesNothingForADocumentThatMaterialXCannotBuild) {
    EXPECT_FALSE(tryGenerate(malformedDocument));
}
