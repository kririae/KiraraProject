#include "mx-prelude.slang"
#include "mx-ggx-reflection-sampler.slang"
#include "libraries/pbrlib/genglsl/mx_conductor_bsdf.glsl"

/// \brief MaterialX's \c mx_conductor_bsdf, sampled by \c GGXReflectionSampler.
public struct ConductorBSDF : IBSDF
{
    public float weight;
    public float3 ior;
    public float3 extinction;
    public float2 alpha;
    public bool retroreflective;
    public float thinfilmThickness;
    public float thinfilmIor;
    public float3 n;
    public float3 x;

    /// The direction that the lobe is sampled and evaluated from.
    float3 view(float3 nf, float3 wo) { return retroreflective ? reflect(-wo, nf) : wo; }

    /// \brief Zero for a \p wi on the other side of the facing normal from \p wo.
    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let nf = mx_forward_facing_normal(n, wo);
        if (dot(nf, wi) <= 0.0)
            return {float3(0.0), 0.0};

        BSDF bsdf = {float3(0.0), float3(1.0)};
        mx_conductor_bsdf(
            makeClosureData(CLOSURE_TYPE_REFLECTION, wi, wo, nf, float3(0.0), 1.0), weight, ior,
            extinction, alpha, retroreflective, thinfilmThickness, thinfilmIor, n, x, 0, bsdf);
        let sampler = GGXReflectionSampler.make(nf, x, alpha);
        return {bsdf.response, sampler.pdf(view(nf, wo), wi)};
    }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        let nf = mx_forward_facing_normal(n, wo);
        let wi = GGXReflectionSampler.make(nf, x, alpha).sample(view(nf, wo), u2);
        if (dot(nf, wi) <= 0.0)
            return {float3(0.0, 0.0, 1.0), 1.0, BSDFLobe.None};
        return {wi, 1.0, BSDFLobe.GlossyReflection};
    }
};

public ConductorBSDF makeConductorBSDF(
    float weight, float3 ior, float3 extinction, float2 roughness, bool retroreflective,
    float thinfilmThickness, float thinfilmIor, float3 n, float3 x)
{
    return {weight,
            ior,
            extinction,
            makeGGXAlpha(roughness),
            retroreflective,
            thinfilmThickness,
            thinfilmIor,
            n,
            x};
}
