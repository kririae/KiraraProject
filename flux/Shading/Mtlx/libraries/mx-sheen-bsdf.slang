#include "mx-prelude.slang"
#include "libraries/pbrlib/genglsl/mx_sheen_bsdf.glsl"

/// \brief MaterialX's \c mx_sheen_bsdf, sampled by the cosine hemisphere sampler.
public struct SheenBSDF : IBSDF
{
    public float weight;
    public float3 color;
    public float roughness;
    public float3 n;
    public int mode;

    /// MaterialX's closure of the reflection from \p wo to \p wi.
    BSDF closure(float3 nf, float3 wo, float3 wi)
    {
        BSDF bsdf = {float3(0.0), float3(1.0)};
        mx_sheen_bsdf(
            makeClosureData(CLOSURE_TYPE_REFLECTION, wi, wo, nf, float3(0.0), 1.0), weight, color,
            roughness, n, mode, bsdf);
        return bsdf;
    }

    /// \brief Zero for a \p wi on the other side of the facing normal from \p wo.
    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let nf = mx_forward_facing_normal(n, wo);
        let nDotL = dot(nf, wi);
        if (nDotL <= 0.0)
            return {float3(0.0), 0.0};

        return {closure(nf, wo, wi).response, cosineHemispherePdf(nDotL)};
    }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        let nf = mx_forward_facing_normal(n, wo);
        return {sampleCosineHemisphere(nf, u2), 1.0, BSDFLobe.DiffuseReflection};
    }

    public float3 throughput(float3 wo)
    {
        let nf = mx_forward_facing_normal(n, wo);
        return saturate(closure(nf, wo, nf).throughput);
    }
};

public SheenBSDF makeSheenBSDF(float weight, float3 color, float roughness, float3 n, int mode)
{
    return {weight, color, roughness, n, mode};
}
