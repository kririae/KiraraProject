#include <gtest/gtest.h>
#include <pxr/imaging/hd/tokens.h>

#include <string>

#include "flux/Hydra/AovMapping.h"

namespace {
pxr::HdRenderPassAovBinding binding(char const *name, char const *sourceName = nullptr) {
    pxr::HdRenderPassAovBinding result;
    result.aovName = pxr::TfToken{name};
    if (sourceName)
        result.aovSettings[pxr::TfToken{"sourceName"}] = std::string{sourceName};
    return result;
}
} // namespace

TEST(AovMappingTests, SourceNameTakesThePlaceOfTheAovName) {
    EXPECT_EQ(HdFluxGetAovChannel(binding("color", "normal")), flux::FilmChannels::Normal);
}

TEST(AovMappingTests, AnLpeSourceLeavesTheAovName) {
    auto lpe = binding("color", "C.*[LO]");
    lpe.aovSettings[pxr::TfToken{"sourceType"}] = pxr::TfToken{"lpe"};
    EXPECT_EQ(HdFluxGetAovChannel(lpe), flux::FilmChannels::Color);
}

TEST(AovMappingTests, MapsTheColorAliasToColor) {
    EXPECT_EQ(HdFluxGetAovChannel(binding("C")), flux::FilmChannels::Color);
}
