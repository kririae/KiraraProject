#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "flux/Slang/SlangCompiler.h"
#include "kira/Anyhow.h"

namespace {
constexpr auto squareLibrary = R"slang(
public float square(float value) { return value * value; }
)slang";

constexpr auto cubeLibrary = R"slang(
public float square(float value) { return value * value * value; }
)slang";

/// \brief The module that the compiler links, and which imports \c toy_library.
constexpr auto entrySource = R"slang(
import toy_library;

[CudaDeviceExport]
public float entryPoint(float value) { return square(value); }
)slang";

[[nodiscard]] flux::SlangModule makeLibrary(std::string_view source) {
    return flux::SlangModule{.name = "toy_library", .source = std::string{source}};
}

[[nodiscard]] flux::SlangModule makeEntry() {
    return flux::SlangModule{.name = "toy_entry", .source = std::string{entrySource}};
}
} // namespace

TEST(SlangCompilerTests, CompilesTheModulesThatTheLinkedModuleImports) {
    auto const compiler = flux::SlangCompiler{flux::SlangCompiler::Target::CudaSource};
    auto const modules = std::vector{makeLibrary(squareLibrary), makeEntry()};

    auto const code = compiler.compile(modules, "toy_entry");

    EXPECT_NE(code.find("__device__"), std::string::npos);
    EXPECT_NE(code.find("entryPoint"), std::string::npos);
    EXPECT_NE(code.find("square"), std::string::npos);
}

TEST(SlangCompilerTests, TheCodeStartsWithThePrelude) {
    auto const compiler = flux::SlangCompiler{flux::SlangCompiler::Target::CudaSource};
    auto const modules = std::vector{makeLibrary(squareLibrary), makeEntry()};

    auto const &prelude = compiler.getTargetPrelude();
    auto const code = compiler.compile(modules, "toy_entry");

    EXPECT_FALSE(prelude.empty());
    EXPECT_TRUE(code.starts_with(prelude));
}

TEST(SlangCompilerTests, ThrowsForAModuleThatFailsToLoad) {
    auto const compiler = flux::SlangCompiler{flux::SlangCompiler::Target::CudaSource};
    auto const modules = std::vector{makeEntry()};

    EXPECT_THROW(static_cast<void>(compiler.compile(modules, "toy_entry")), kira::Anyhow);
}

TEST(SlangCompilerTests, ThrowsForTwoModulesOfOneName) {
    auto const compiler = flux::SlangCompiler{flux::SlangCompiler::Target::CudaSource};
    auto const modules =
        std::vector{makeLibrary(squareLibrary), makeLibrary(cubeLibrary), makeEntry()};

    EXPECT_THROW(static_cast<void>(compiler.compile(modules, "toy_entry")), std::invalid_argument);
}

TEST(SlangCompilerTests, CarriesNoModuleFromOneCompileToTheNext) {
    auto const compiler = flux::SlangCompiler{flux::SlangCompiler::Target::CudaSource};

    auto const squares =
        compiler.compile(std::vector{makeLibrary(squareLibrary), makeEntry()}, "toy_entry");
    auto const cubes =
        compiler.compile(std::vector{makeLibrary(cubeLibrary), makeEntry()}, "toy_entry");

    EXPECT_NE(squares, cubes);
}
