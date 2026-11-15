#pragma once

#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>

#include <optional>
#include <string>
#include <vector>

class HdFLuxRenderer;
class HdFLuxRenderParam;

/// \brief The interface to execute the rendering pipeline of FLux from Hydra.
class HdFLuxRenderPass final : public pxr::HdRenderPass {
public:
    HdFLuxRenderPass(
        pxr::HdRenderIndex *index, pxr::HdRprimCollection const &collection,
        HdFLuxRenderParam &param, HdFLuxRenderer &renderer
    );

    /// \brief Returns whether the image has all the requested samples, the pass renders nothing, or
    /// the delegate has failed.
    bool IsConverged() const override;

private:
    void _Execute(
        pxr::HdRenderPassStateSharedPtr const &renderPassState, pxr::TfTokenVector const &renderTags
    ) override;

    HdFLuxRenderParam &param_;
    HdFLuxRenderer &renderer_;
    std::optional<unsigned int> settingsVersion_;
    std::vector<std::string> warnings_;
};
