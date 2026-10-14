#include "flux/Hydra/RenderPass.h"

#include "flux/Hydra/RenderBuffer.h"

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
            buffer->Clear(binding.clearValue);
            buffer->Unmap();
        }
        buffer->SetConverged(true);
    }
}
