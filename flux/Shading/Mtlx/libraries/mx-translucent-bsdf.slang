#include "mx-prelude.slang"
#include "libraries/pbrlib/genglsl/mx_translucent_bsdf.glsl"

/// \brief MaterialX's \c mx_translucent_bsdf, sampled by the cosine hemisphere sampler.
///
/// It scatters into the hemisphere that the facing normal points away from.
public struct TranslucentBSDF : IBSDF
{
    public float weight;
    public float3 color;
    public float3 n;

    /// \brief Zero for a \p wi on the same side of the facing normal as \p wo.
    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let nf = mx_forward_facing_normal(n, wo);
        let nDotL = -dot(nf, wi);
        if (nDotL <= 0.0)
            return {float3(0.0), 0.0};

        BSDF bsdf = {float3(0.0), float3(1.0)};
        mx_translucent_bsdf(
            makeClosureData(CLOSURE_TYPE_REFLECTION, wi, wo, nf, float3(0.0), 1.0), weight, color,
            nf, bsdf);
        return {bsdf.response, cosineHemispherePdf(nDotL)};
    }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        let nf = mx_forward_facing_normal(n, wo);
        return {sampleCosineHemisphere(-nf, u2), 1.0, BSDFLobe.DiffuseTransmission};
    }

    public float3 throughput(float3 wo) { return float3(0.0); }
};

public TranslucentBSDF makeTranslucentBSDF(float weight, float3 color, float3 n)
{
    return {weight, color, n};
}
