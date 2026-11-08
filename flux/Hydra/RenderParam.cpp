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

flux::ShaderInstance generateFallbackShader(flux::MtlxShaderGenerator const &generator) {
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
            "fallback", {}
        )
        .instance.value();
}

flux::Ref<flux::ShaderBSDF>
createFallbackBSDF(flux::Context &context, flux::ShaderInstance instance) {
    return context.create<flux::ShaderBSDF>(instance.spec, std::move(instance.words));
}
} // namespace

HdFluxRenderParam::HdFluxRenderParam(
    std::filesystem::path const &modulePath, std::filesystem::path const &mtlxRoot
)
    : handler_{makeContext(), modulePath}, shaderGenerator_{mtlxRoot},
      fallbackShaderInstance_{generateFallbackShader(shaderGenerator_)},
      fallbackBSDF_{createFallbackBSDF(*handler_.getContext(), fallbackShaderInstance_)} {}

HdFluxRenderParam::~HdFluxRenderParam() = default;

flux::Ref<flux::Context> HdFluxRenderParam::GetContext() const { return handler_.getContext(); }

flux::Ref<flux::BSDF const> HdFluxRenderParam::GetFallbackBSDF() const { return fallbackBSDF_; }

flux::ShaderInstance const &HdFluxRenderParam::GetFallbackShaderInstance() const noexcept {
    return fallbackShaderInstance_;
}

void HdFluxRenderParam::ReportFailure() noexcept {
    failed_ = true;

    try {
        throw;
    } catch (std::exception const &error) {
        TF_RUNTIME_ERROR("The delegate failed: %s", error.what());
    } catch (...) { TF_RUNTIME_ERROR("The delegate failed"); }
}
