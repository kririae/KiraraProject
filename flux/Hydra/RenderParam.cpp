#include "flux/Hydra/RenderParam.h"

#include <pxr/base/tf/diagnostic.h>

#include <exception>
#include <memory>
#include <utility>

#include "flux/Hydra/ArFileSystem.h"
#include "flux/Hydra/Material.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Properties.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
flux::Ref<flux::Context> makeContext() {
    auto context = flux::Context::create(std::make_unique<HdFLuxArFileSystem>());
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

HdFLuxRenderParam::HdFLuxRenderParam(std::filesystem::path const &mtlxRoot)
    : handler_{makeContext()}, shaderGenerator_{mtlxRoot},
      fallbackShaderInstance_{generateFallbackShader(shaderGenerator_)},
      fallbackBSDF_{createFallbackBSDF(*handler_.getContext(), fallbackShaderInstance_)} {}

HdFLuxRenderParam::~HdFLuxRenderParam() = default;

flux::Ref<flux::Context> HdFLuxRenderParam::GetContext() const { return handler_.getContext(); }

flux::Ref<flux::BSDF const> HdFLuxRenderParam::GetFallbackBSDF() const { return fallbackBSDF_; }

flux::ShaderInstance const &HdFLuxRenderParam::GetFallbackShaderInstance() const noexcept {
    return fallbackShaderInstance_;
}

void HdFLuxRenderParam::AddPendingMaterial(HdFLuxPendingMaterial material) {
    pendingMaterials_.push_back(std::move(material));
}

std::vector<HdFLuxPendingMaterial> HdFLuxRenderParam::ExtractPendingMaterials() noexcept {
    return std::exchange(pendingMaterials_, {});
}

void HdFLuxRenderParam::SetProgress(double percentDone, double totalClockTime) noexcept {
    percentDone_ = percentDone;
    totalClockTime_ = totalClockTime;
}

void HdFLuxRenderParam::ReportFailure() noexcept {
    failed_ = true;

    try {
        throw;
    } catch (std::exception const &error) {
        TF_RUNTIME_ERROR("The delegate failed: %s", error.what());
    } catch (...) { TF_RUNTIME_ERROR("The delegate failed"); }
}
