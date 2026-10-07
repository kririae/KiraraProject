#pragma once

#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>

/// \brief The render pass of the FLux delegate.
///
/// Fills the AOV buffers bound at each execution.
class HdFluxRenderPass final : public pxr::HdRenderPass {
public:
    using pxr::HdRenderPass::HdRenderPass;

    /// \brief Returns whether every buffer bound at the last execution has converged.
    bool IsConverged() const override;

private:
    void _Execute(
        pxr::HdRenderPassStateSharedPtr const &renderPassState, pxr::TfTokenVector const &renderTags
    ) override;

    /// AOV bindings of the last execution.
    pxr::HdRenderPassAovBindingVector aovBindings_;
};
