#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "flux/Slang/SlangCompiler.h"

namespace {
/// \brief A backend that mixes two leaves of the library, with the closure declared by \c let.
constexpr auto backendSource = R"slang(
import flux_material;

[CudaDeviceExport]
public BSDFResult entry(
    uint* words, float3 normal, float3 wo, float3 wi, bool eval, float u1, float2 u2)
{
    let bsdf = makeMixBSDF(EmptyBSDF(), EmptyBSDF(), asfloat(words[0]));
    return execute(bsdf, wo, wi, eval, u1, u2);
}
)slang";

[[nodiscard]] std::string readLibrary() {
    auto const path = std::string{FLUX_SLANG_DIR} + "/flux-material.slang";
    auto stream = std::ifstream{path};
    if (!stream)
        throw std::runtime_error{"cannot read " + path};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}
} // namespace

TEST(SlangMaterialLibraryTests, CompilesAMixOfTwoLeavesIntoOneConcreteType) {
    auto const compiler = flux::SlangCompiler{flux::SlangCompiler::Target::CudaSource};
    auto const modules = std::vector{
        flux::SlangModule{.name = "flux_material", .source = readLibrary()},
        flux::SlangModule{.name = "backend", .source = backendSource},
    };

    auto const code = compiler.compile(modules, "backend");

    EXPECT_EQ(code.find("AnyValue"), std::string::npos);
}
