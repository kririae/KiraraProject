#include "flux/Hydra/RenderPass.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/tokens.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "flux/Hydra/AovMapping.h"
#include "flux/Hydra/RenderBuffer.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/RenderSettings.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
/// \brief What a pass asks the renderer to render, and the warnings about what it cannot.
struct PassRequest {
    HdFluxRenderSpec spec;
    std::vector<HdFluxRenderer::Output> outputs;
    std::vector<std::string> warnings;
};

[[nodiscard]] PassRequest makeRequest(pxr::HdRenderPassState const &renderPassState) {
    PassRequest request;
    bool hasResolution{false};

    for (auto const &binding : renderPassState.GetAovBindings()) {
        if (!binding.renderBuffer)
            continue;

        auto *const buffer = dynamic_cast<HdFluxRenderBuffer *>(binding.renderBuffer);
        if (!buffer)
            throw std::invalid_argument(
                "HdFluxRenderPass: an AOV buffer is not an HdFluxRenderBuffer"
            );

        // A buffer that was not allocated has no pixels to fill, but its converged state is set.
        if (buffer->GetWidth() == 0 || buffer->GetHeight() == 0) {
            request.outputs.push_back({buffer, flux::FilmChannels::None, binding.clearValue});
            continue;
        }

        auto channel = HdFluxGetAovChannel(binding);
        if (channel == flux::FilmChannels::None) {
            request.warnings.push_back(
                pxr::TfStringPrintf(
                    "AOV %s is not rendered and keeps its clear value", binding.aovName.GetText()
                )
            );
        } else {
            flux::Vec2u const resolution{buffer->GetWidth(), buffer->GetHeight()};

            if (!hasResolution) {
                hasResolution = true;
                request.spec.resolution = resolution;
            } else if (!(request.spec.resolution == resolution)) {
                channel = flux::FilmChannels::None;
                request.warnings.push_back(
                    pxr::TfStringPrintf(
                        "AOV %s differs in size from the first AOV and is skipped",
                        binding.aovName.GetText()
                    )
                );
            }
        }

        request.spec.channels = request.spec.channels | channel;
        request.outputs.push_back({buffer, channel, binding.clearValue});
    }

    auto const *const camera = renderPassState.GetCamera();
    if (camera && camera->GetProjection() != pxr::HdCamera::Perspective) {
        request.warnings.emplace_back("An orthographic camera is not rendered");
    } else if (camera) {
        request.spec.view = HdFluxRenderSpec::View{
            renderPassState.GetWorldToViewMatrix(), renderPassState.GetProjectionMatrix()
        };
    }

    return request;
}
} // namespace

HdFluxRenderPass::HdFluxRenderPass(
    pxr::HdRenderIndex *index, pxr::HdRprimCollection const &collection, HdFluxRenderParam &param
)
    : pxr::HdRenderPass(index, collection), param_{param}, renderer_{param} {}

bool HdFluxRenderPass::IsConverged() const { return renderer_.IsConverged(); }

void HdFluxRenderPass::_Execute(
    pxr::HdRenderPassStateSharedPtr const &renderPassState, pxr::TfTokenVector const &
) try {
    auto *const delegate = GetRenderIndex()->GetRenderDelegate();
    if (auto const version = delegate->GetRenderSettingsVersion(); settingsVersion_ != version) {
        settingsVersion_ = version;

        static pxr::TfToken const huskRenderMode{"renderMode"};
        auto const batch = !delegate->GetRenderSetting<bool>(
                               pxr::HdRenderSettingsTokens->enableInteractive, true
                           ) ||
                           delegate->GetRenderSetting<std::string>(huskRenderMode, {}) == "batch";

        renderer_.SetSchedule(HdFluxSampleSchedule(
            delegate->GetRenderSetting<int>(
                pxr::HdRenderSettingsTokens->convergedSamplesPerPixel,
                HdFluxDefaultConvergedSamplesPerPixel
            ),
            delegate->GetRenderSetting<int>(
                HdFluxRenderSettingsTokens->interactiveSamplesPerLaunch,
                HdFluxDefaultInteractiveSamplesPerLaunch
            ),
            batch ? HdFluxSampleSchedule::Mode::Batch : HdFluxSampleSchedule::Mode::Interactive
        ));
    }

    auto request = makeRequest(*renderPassState);

    if (request.warnings != warnings_) {
        for (auto const &warning : request.warnings)
            TF_WARN(warning);
        warnings_ = std::move(request.warnings);
    }

    renderer_.Render(request.spec, request.outputs);
} catch (...) { param_.ReportFailure(); }
