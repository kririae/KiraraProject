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

// The renderer resolves its samples itself, so no buffer is multisampled.
std::array<Aov, 3> const &aovs() {
    static std::array<Aov, 3> const table{{
        {pxr::HdAovTokens->color,
         flux::FilmChannels::Color,
         {pxr::HdFormatFloat32Vec4, false, pxr::VtValue(pxr::GfVec4f(0.0F))}},
        {pxr::HdAovTokens->normal,
         flux::FilmChannels::Normal,
         {pxr::HdFormatFloat32Vec3, false, pxr::VtValue(pxr::GfVec3f(0.0F))}},
        {pxr::TfToken{"albedo"},
         flux::FilmChannels::Albedo,
         {pxr::HdFormatFloat32Vec3, false, pxr::VtValue(pxr::GfVec3f(0.0F))}},
    }};
    return table;
}

[[nodiscard]] Aov const *find(pxr::TfToken const &name) {
    // A host convention: some hosts, such as Houdini, name the color AOV C, not color.
    static pxr::TfToken const colorAlias{"C"};
    auto const &key = name == colorAlias ? pxr::HdAovTokens->color : name;

    auto const &table = aovs();
    auto const found = std::ranges::find(table, key, &Aov::name);
    return found == table.end() ? nullptr : &*found;
}
} // namespace

flux::FilmChannels HdFluxGetAovChannel(pxr::HdRenderPassAovBinding const &binding) {
    auto name = binding.aovName;

    if (auto const source = binding.aovSettings.find(pxr::TfToken{"sourceName"});
        source != binding.aovSettings.end() && source->second.IsHolding<std::string>())
        name = pxr::TfToken{source->second.UncheckedGet<std::string>()};

    auto const *const aov = find(name);
    return aov ? aov->channel : flux::FilmChannels::None;
}

pxr::HdAovDescriptor HdFluxGetAovDescriptor(pxr::TfToken const &name) {
    auto const *const aov = find(name);
    return aov ? aov->descriptor : pxr::HdAovDescriptor{};
}
