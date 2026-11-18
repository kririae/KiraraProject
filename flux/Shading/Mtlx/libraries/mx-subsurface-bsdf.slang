#include "mx-prelude.slang"
#include "libraries/pbrlib/genglsl/lib/mx_microfacet_diffuse.glsl"

/// \brief The reflection of MaterialX's \c mx_subsurface_bsdf, sampled by the cosine hemisphere
/// sampler.
///
/// The radius of the surface is the one that MaterialX takes for a flat surface.
public struct SubsurfaceBSDF : IBSDF
{
    static const float flatSurfaceRadius = 1.0 / 0.01;

    public float weight;
    public float3 color;
    public float3 meanFreePath;
    public float3 n;

    /// \brief Zero for a \p wi on the other side of the facing normal from \p wo.
    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let nf = mx_forward_facing_normal(n, wo);
        let nDotL = dot(nf, wi);
        if (nDotL <= 0.0 || weight < M_FLOAT_EPS)
            return {float3(0.0), 0.0};

        let profile = mx_integrate_burley_diffusion(nf, wi, flatSurfaceRadius, meanFreePath);
        return {weight * color * profile / M_PI, cosineHemispherePdf(nDotL)};
    }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        let nf = mx_forward_facing_normal(n, wo);
        return {sampleCosineHemisphere(nf, u2), 1.0, BSDFLobe.DiffuseReflection};
    }

    public float3 throughput(float3 wo) { return float3(0.0); }
};

public SubsurfaceBSDF makeSubsurfaceBSDF(float weight, float3 color, float3 meanFreePath, float3 n)
{
    return {weight, color, meanFreePath, n};
}
