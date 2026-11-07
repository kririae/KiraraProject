#include "mx-prelude.slang"
#include "mx-ggx-reflection-sampler.slang"
#include "libraries/pbrlib/genglsl/lib/mx_transmission_opacity.glsl"
#include "libraries/pbrlib/genglsl/mx_dielectric_bsdf.glsl"

/// \brief The reflection of MaterialX's \c mx_dielectric_bsdf, sampled by \c GGXReflectionSampler.
///
/// The scatter modes that transmit add no transmission.
public struct DielectricBSDF : IBSDF
{
    public float weight;
    public float3 tint;
    public float ior;
    public float2 alpha;
    public bool retroreflective;
    public float thinfilmThickness;
    public float thinfilmIor;
    public float3 n;
    public float3 x;
    public int distribution;
    public int scatterMode;

    /// The direction that the lobe is sampled and evaluated from.
    float3 view(float3 nf, float3 wo) { return retroreflective ? reflect(-wo, nf) : wo; }

    /// MaterialX's closure of the reflection from \p wo to \p wi.
    BSDF closure(float3 nf, float3 wo, float3 wi)
    {
        BSDF bsdf = {float3(0.0), float3(1.0)};
        mx_dielectric_bsdf(
            makeClosureData(CLOSURE_TYPE_REFLECTION, wi, wo, nf, float3(0.0), 1.0), weight, tint,
            ior, alpha, retroreflective, thinfilmThickness, thinfilmIor, n, x, distribution,
            scatterMode, bsdf);
        return bsdf;
    }

    /// \brief Zero for a \p wi on the other side of the facing normal from \p wo.
    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let nf = mx_forward_facing_normal(n, wo);
        if (dot(nf, wi) <= 0.0)
            return {float3(0.0), 0.0};

        let sampler = GGXReflectionSampler.make(nf, x, alpha);
        return {closure(nf, wo, wi).response, sampler.pdf(view(nf, wo), wi)};
    }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        let nf = mx_forward_facing_normal(n, wo);
        let sampler = GGXReflectionSampler.make(nf, x, alpha);
        return {sampler.sample(view(nf, wo), u2), 1.0, BSDFLobe.GlossyReflection};
    }

    public float3 throughput(float3 wo)
    {
        let nf = mx_forward_facing_normal(n, wo);
        return saturate(closure(nf, wo, nf).throughput);
    }
};

public DielectricBSDF makeDielectricBSDF(
    float weight, float3 tint, float ior, float2 roughness, bool retroreflective,
    float thinfilmThickness, float thinfilmIor, float3 n, float3 x, int distribution,
    int scatterMode)
{
    return {weight,
            tint,
            ior,
            makeGGXAlpha(roughness),
            retroreflective,
            thinfilmThickness,
            thinfilmIor,
            n,
            x,
            distribution,
            scatterMode};
}
