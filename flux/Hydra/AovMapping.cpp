#include "flux/Hydra/AovMapping.h"

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hd/tokens.h>

#include <algorithm>
#include <array>
#include <string>

namespace {
struct Aov {
    pxr::TfToken name;
    flux::FilmChannels channel;
    pxr::HdAovDescriptor descriptor;
};

std::array<Aov, 4> const &aovs() {
    static std::array<Aov, 4> const table{{
        {pxr::HdAovTokens->color,
         flux::FilmChannels::Color,
         {pxr::HdFormatFloat32Vec4, false, pxr::VtValue(pxr::GfVec4f(0.0F))}},
        {pxr::HdAovTokens->normal,
         flux::FilmChannels::Normal,
         {pxr::HdFormatFloat32Vec3, false, pxr::VtValue(pxr::GfVec3f(0.0F))}},
        {pxr::TfToken{"albedo"},
         flux::FilmChannels::Albedo,
         {pxr::HdFormatFloat32Vec3, false, pxr::VtValue(pxr::GfVec3f(0.0F))}},
        {pxr::HdAovTokens->depth,
         flux::FilmChannels::Depth,
         {pxr::HdFormatFloat32, false, pxr::VtValue(1.0F)}},
    }};
    return table;
}

[[nodiscard]] Aov const *find(pxr::TfToken const &name) {
    static pxr::TfToken const colorAlias{"C"};
    auto const &key = name == colorAlias ? pxr::HdAovTokens->color : name;

    auto const &table = aovs();
    auto const found = std::ranges::find(table, key, &Aov::name);
    return found == table.end() ? nullptr : &*found;
}
} // namespace

flux::FilmChannels HdFluxGetAovChannel(pxr::HdRenderPassAovBinding const &binding) {
    static pxr::TfToken const sourceNameKey{"sourceName"};
    static pxr::TfToken const sourceTypeKey{"sourceType"};
    static pxr::TfToken const raw{"raw"};

    auto const &settings = binding.aovSettings;
    auto const sourceType = settings.find(sourceTypeKey);
    auto const isRaw =
        sourceType == settings.end() || (sourceType->second.IsHolding<pxr::TfToken>() &&
                                         sourceType->second.UncheckedGet<pxr::TfToken>() == raw);

    auto name = binding.aovName;
    if (auto const source = settings.find(sourceNameKey);
        isRaw && source != settings.end() && source->second.IsHolding<std::string>())
        name = pxr::TfToken{source->second.UncheckedGet<std::string>()};

    auto const *const aov = find(name);
    return aov ? aov->channel : flux::FilmChannels::None;
}

pxr::HdAovDescriptor HdFluxGetAovDescriptor(pxr::TfToken const &name) {
    auto const *const aov = find(name);
    return aov ? aov->descriptor : pxr::HdAovDescriptor{};
}
