#pragma once

#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>

#include <optional>
#include <string>
#include <vector>

#include "flux/Hydra/Renderer.h"

class HdFluxRenderParam;

/// \brief The render pass of the FLux delegate.
class HdFluxRenderPass final : public pxr::HdRenderPass {
public:
    /// \pre \p param outlives the pass.
    HdFluxRenderPass(
        pxr::HdRenderIndex *index, pxr::HdRprimCollection const &collection,
        HdFluxRenderParam &param
    );

    /// \brief Returns whether the image has all the requested samples, the pass renders nothing, or
    /// the delegate has failed.
    bool IsConverged() const override;

private:
    void _Execute(
        pxr::HdRenderPassStateSharedPtr const &renderPassState, pxr::TfTokenVector const &renderTags
    ) override;

    HdFluxRenderParam &param_;
    HdFluxRenderer renderer_;
    std::optional<unsigned int> settingsVersion_;
    std::vector<std::string> warnings_;
};
