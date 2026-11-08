import flux_bsdf;
import flux_context;
import material;

__target_intrinsic(cuda, "flux::OptixBSDFResult") struct OptixBSDFResult
{
    __target_intrinsic(cuda, "evaluation.value") float3 evaluationValue;
    __target_intrinsic(cuda, "evaluation.pdf") float evaluationPdf;
    __target_intrinsic(cuda, "sample.weight") float3 sampleWeight;
    __target_intrinsic(cuda, "sample.wi") float3 sampleWi;
    __target_intrinsic(cuda, "sample.pdf") float samplePdf;
    __target_intrinsic(cuda, "sample.eta") float sampleEta;
    __target_intrinsic(cuda, "sample.lobe") uint sampleLobe;
};

[CudaDeviceExport]
OptixBSDFResult fluxExecute(
    uint* words, ShadingContext ctx, float3 wo, float3 wi, bool eval, float u1, float2 u2)
{
    let result = execute(makeBSDF(words, ctx), wo, wi, eval, u1, u2);
    OptixBSDFResult converted;
    converted.evaluationValue = result.evaluation.value;
    converted.evaluationPdf = result.evaluation.pdf;
    converted.sampleWeight = result.sample.weight;
    converted.sampleWi = result.sample.wi;
    converted.samplePdf = result.sample.pdf;
    converted.sampleEta = result.sample.eta;
    converted.sampleLobe = uint(result.sample.lobe);
    return converted;
}

[CudaDeviceExport]
float3 fluxEvaluateEDF(uint* words, ShadingContext ctx, float3 wo)
{
    return makeEDF(words, ctx).evaluate(fluxToShadingFrame(ctx, wo));
}
