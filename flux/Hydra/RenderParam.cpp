#include "flux/Hydra/RenderParam.h"

#include <pxr/base/tf/diagnostic.h>

#include <exception>
#include <utility>

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

HdFluxMaterialXGenerator::Material makeFallbackShader(HdFluxMaterialXGenerator const &generator) {
    return generator
        .generate(
            R"(<?xml version="1.0"?>
<materialx version="1.39">
  <oren_nayar_diffuse_bsdf name="diffuse" type="BSDF">
    <input name="color" type="color3" value="0.18, 0.18, 0.18" />
    <input name="roughness" type="float" value="0" />
  </oren_nayar_diffuse_bsdf>
  <surface name="surface" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="diffuse" />
  </surface>
  <surfacematerial name="fallback" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="surface" />
  </surfacematerial>
</materialx>)",
            "fallback"
        )
        .value();
}

flux::Ref<flux::ShaderBSDF>
makeFallbackBSDF(flux::Context &context, HdFluxMaterialXGenerator::Material fallbackShader) {
    return context.create<flux::ShaderBSDF>(fallbackShader.spec, std::move(fallbackShader.words));
}
} // namespace

HdFluxRenderParam::HdFluxRenderParam(
    std::filesystem::path const &modulePath, std::filesystem::path const &resourcePath
)
    : handler_{makeContext(), modulePath}, materialXGenerator_{resourcePath},
      fallbackShader_{makeFallbackShader(materialXGenerator_)},
      fallbackBSDF_{makeFallbackBSDF(*handler_.getContext(), fallbackShader_)} {}

HdFluxRenderParam::~HdFluxRenderParam() = default;

flux::Ref<flux::Context> HdFluxRenderParam::GetContext() const { return handler_.getContext(); }

flux::Ref<flux::BSDF const> HdFluxRenderParam::GetFallbackBSDF() const { return fallbackBSDF_; }

HdFluxMaterialXGenerator::Material const &HdFluxRenderParam::GetFallbackShader() const noexcept {
    return fallbackShader_;
}

void HdFluxRenderParam::ReportFailure() noexcept {
    failed_ = true;

    try {
        throw;
    } catch (std::exception const &error) {
        TF_RUNTIME_ERROR("The delegate failed: %s", error.what());
    } catch (...) { TF_RUNTIME_ERROR("The delegate failed"); }
}
