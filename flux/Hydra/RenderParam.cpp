#include "flux/Hydra/RenderParam.h"

#include <pxr/base/tf/diagnostic.h>

#include <exception>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/BSDF.h"
#include "kira/Properties.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
flux::Ref<flux::Context> makeContext() {
    auto context = flux::Context::create();
    context->setActiveIntegrator(context->create<flux::PathIntegrator>(kira::Properties{}));
    context->setActiveSampler(context->create<flux::IndependentSampler>(kira::Properties{}));
    return context;
}

flux::Ref<flux::BSDF const> makeFallbackBSDF(flux::Context &context) {
    kira::Properties props;
    props.set("R", 0.5F);
    return context.create<flux::DiffuseBSDF>(props);
}
} // namespace

HdFluxRenderParam::HdFluxRenderParam(std::filesystem::path const &modulePath)
    : handler_{makeContext(), modulePath}, fallbackBSDF_{makeFallbackBSDF(*handler_.getContext())} {
}

HdFluxRenderParam::~HdFluxRenderParam() = default;

flux::Ref<flux::Context> HdFluxRenderParam::GetContext() const { return handler_.getContext(); }

flux::Ref<flux::BSDF const> HdFluxRenderParam::GetFallbackBSDF() const { return fallbackBSDF_; }

void HdFluxRenderParam::ReportFailure() noexcept {
    failed_ = true;

    try {
        throw;
    } catch (std::exception const &error) {
        TF_RUNTIME_ERROR("The delegate failed: %s", error.what());
    } catch (...) { TF_RUNTIME_ERROR("The delegate failed"); }
}
