#include "flux/Hydra/RenderPass.h"

#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hd/types.h>

#include "flux/Hydra/RenderBuffer.h"

namespace {
/// \brief Returns \p value as a color with alpha. A color without alpha is opaque, and any other
///        value is transparent black.
[[nodiscard]] pxr::GfVec4f toClearColor(pxr::VtValue const &value) {
    auto const type = pxr::HdGetValueTupleType(value);
    if (type.count != 1)
        return pxr::GfVec4f(0.0F);
    auto const *data = pxr::HdGetValueData(value);
    switch (type.type) {
    case pxr::HdTypeFloatVec3: {
        auto const &color = *static_cast<pxr::GfVec3f const *>(data);
        return {color[0], color[1], color[2], 1.0F};
    }
    case pxr::HdTypeFloatVec4: return *static_cast<pxr::GfVec4f const *>(data);
    case pxr::HdTypeDoubleVec3: {
        auto const &color = *static_cast<pxr::GfVec3d const *>(data);
        return pxr::GfVec4f(pxr::GfVec4d(color[0], color[1], color[2], 1.0));
    }
    case pxr::HdTypeDoubleVec4: return pxr::GfVec4f(*static_cast<pxr::GfVec4d const *>(data));
    default: return pxr::GfVec4f(0.0F);
    }
}
} // namespace

bool HdFluxRenderPass::IsConverged() const {
    for (auto const &binding : aovBindings_)
        if (binding.renderBuffer && !binding.renderBuffer->IsConverged())
            return false;
    return true;
}

void HdFluxRenderPass::_Execute(
    pxr::HdRenderPassStateSharedPtr const &renderPassState, pxr::TfTokenVector const &
) {
    aovBindings_ = renderPassState->GetAovBindings();
    for (auto const &binding : aovBindings_) {
        auto *const buffer = static_cast<HdFluxRenderBuffer *>(binding.renderBuffer);
        if (!buffer)
            continue;
        // An empty clear value leaves the buffer as it is.
        if (!binding.clearValue.IsEmpty()) {
            buffer->Map();
            buffer->Clear(toClearColor(binding.clearValue));
            buffer->Unmap();
        }
        buffer->SetConverged(true);
    }
}
