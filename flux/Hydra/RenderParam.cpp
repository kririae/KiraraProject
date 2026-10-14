#include "flux/Hydra/RenderParam.h"

#include <pxr/base/tf/diagnostic.h>

#include <exception>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Context.h"
#include "kira/Properties.h"

// The Tf diagnostic macros name Tf without its namespace.
PXR_NAMESPACE_USING_DIRECTIVE

namespace {
flux::Ref<flux::Context> makeContext() {
    auto context = flux::Context::create();
    context->setActiveIntegrator(context->create<flux::PathIntegrator>(kira::Properties{}));
    context->setActiveSampler(context->create<flux::IndependentSampler>(kira::Properties{}));
    return context;
}
} // namespace

HdFluxRenderParam::HdFluxRenderParam(std::filesystem::path const &modulePath)
    : handler_{makeContext(), modulePath} {}

HdFluxRenderParam::~HdFluxRenderParam() = default;

flux::Ref<flux::Context> HdFluxRenderParam::GetContext() const { return handler_.getContext(); }

void HdFluxRenderParam::ReportFailure() noexcept {
    failed_ = true;

    try {
        throw;
    } catch (std::exception const &error) {
        TF_RUNTIME_ERROR("The delegate failed: %s", error.what());
    } catch (...) { TF_RUNTIME_ERROR("The delegate failed"); }
}
