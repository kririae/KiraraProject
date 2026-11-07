#include "mx-prelude.slang"
#include "mx-ggx-refraction-sampler.slang"
#include "libraries/pbrlib/genglsl/lib/mx_transmission_opacity.glsl"
#include "libraries/pbrlib/genglsl/mx_dielectric_bsdf.glsl"

static const int scatterModeR = 0;
static const int scatterModeT = 1;
static const int scatterModeRT = 2;

/// \brief MaterialX's \c dielectric_bsdf, with the microfacet BTDF of Walter et al. 2007.
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
    public float3 ng;
    public int distribution;
    public int scatterMode;

    /// The index of refraction across the surface from \p wo over that on its side.
    float eta(float3 wo) { return dot(wo, ng) >= 0.0 ? ior : 1.0 / ior; }

    bool transmits() { return scatterMode != scatterModeR; }

    /// The direction that the reflection is sampled and evaluated from.
    float3 view(float3 nf, float3 wo) { return retroreflective ? reflect(-wo, nf) : wo; }

    GGXRefractionSampler refractionSampler(float3 nf, float3 wo)
    {
        return GGXRefractionSampler.make(nf, x, alpha, eta(wo), scatterMode == scatterModeRT);
    }

    /// MaterialX's closure of the reflection from \p wo to \p wi.
    BSDF closure(float3 nf, float3 wo, float3 wi)
    {
        BSDF bsdf = {float3(0.0), float3(1.0)};
        mx_dielectric_bsdf(
            makeClosureData(CLOSURE_TYPE_REFLECTION, wi, wo, nf, float3(0.0), 1.0), weight, tint,
            eta(wo), alpha, retroreflective, thinfilmThickness, thinfilmIor, n, x, distribution,
            scatterMode, bsdf);
        return bsdf;
    }

    /// The BTDF times the cosine from \p wo to \p wi, which lie on the two sides of \p nf.
    float3 transmission(GGXRefractionSampler sampler, float3 nf, float3 wo, float3 wi)
    {
        let geometry = sampler.refraction(wo, wi);
        if (weight < M_FLOAT_EPS || !geometry.valid)
            return float3(0.0);

        let nDotV = clamp(dot(nf, wo), M_FLOAT_EPS, 1.0);
        let nDotL = clamp(-dot(nf, wi), M_FLOAT_EPS, 1.0);
        let D = mx_ggx_NDF(sampler.visible.toLocal(geometry.h), alpha);
        let G = mx_ggx_smith_G2(nDotL, nDotV, mx_average_alpha(alpha));
        let transmitted = 1.0 - sampler.reflectionProbability(geometry.vDotH);
        let radianceScale = 1.0 / (sampler.eta * sampler.eta);
        return max(tint, 0.0) * weight * transmitted * D * G * geometry.vDotH / nDotV *
               geometry.dHdWi * radianceScale;
    }

    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let nf = mx_forward_facing_normal(n, wo);
        if (!transmits())
        {
            if (dot(nf, wi) <= 0.0)
                return {float3(0.0), 0.0};

            let sampler = GGXReflectionSampler.make(nf, x, alpha);
            return {closure(nf, wo, wi).response, sampler.pdf(view(nf, wo), wi)};
        }

        if (eta(wo) == 1.0)
            return {float3(0.0), 0.0};

        let sampler = refractionSampler(nf, wo);
        let value = dot(nf, wi) > 0.0 ? closure(nf, wo, wi).response
                                      : transmission(sampler, nf, wo, wi);
        return {value, sampler.pdf(wo, wi)};
    }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        let nf = mx_forward_facing_normal(n, wo);
        if (transmits())
        {
            if (eta(wo) == 1.0)
            {
                let value = weight < M_FLOAT_EPS ? float3(0.0) : max(tint, 0.0) * weight;
                return {-wo, 1.0, BSDFLobe.DeltaTransmission, {value, 1.0}};
            }
            return refractionSampler(nf, wo).sample(wo, u1, u2);
        }

        let sampler = GGXReflectionSampler.make(nf, x, alpha);
        let wi = sampler.sample(view(nf, wo), u2);
        if (dot(nf, wi) <= 0.0)
            return {float3(0.0, 0.0, 1.0), 1.0, BSDFLobe.None};
        return {wi, 1.0, BSDFLobe.GlossyReflection};
    }

    /// From the inside, the reflected energy is the exact Fresnel reflectance, not MaterialX's fit.
    public float3 throughput(float3 wo)
    {
        if (scatterMode == scatterModeT)
            return float3(1.0);

        let nf = mx_forward_facing_normal(n, wo);
        if (dot(wo, ng) >= 0.0)
            return saturate(closure(nf, wo, nf).throughput);

        let nDotV = clamp(dot(nf, wo), M_FLOAT_EPS, 1.0);
        return float3(saturate(1.0 - weight * mx_fresnel_dielectric(nDotV, eta(wo))));
    }
};

public DielectricBSDF makeDielectricBSDF(
    float weight, float3 tint, float ior, float2 roughness, bool retroreflective,
    float thinfilmThickness, float thinfilmIor, float3 n, float3 x, float3 ng, int distribution,
    int scatterMode)
{
    return {weight,
            tint,
            ior,
            makeGGXAlpha(roughness),
            retroreflective && scatterMode == scatterModeR,
            thinfilmThickness,
            thinfilmIor,
            n,
            x,
            ng,
            distribution,
            scatterMode};
}
