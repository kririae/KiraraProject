#include "mx-ggx-reflection-sampler.slang"

/// \brief The half vector that refracts \c v into \c wi.
struct RefractionGeometry
{
    /// Whether \c v and \c wi lie on the two sides of the half vector \c h.
    bool valid;

    float3 h;
    float vDotH;
    float wiDotH;

    /// The solid-angle density of \c wi per unit of the solid-angle density of \c h.
    float dHdWi;
};

/// \brief Reflects or refracts a direction about a visible normal of a GGX distribution.
///
/// A sample that lands on the wrong side of \c visible.n is a null event.
struct GGXRefractionSampler
{
    GGXReflectionSampler visible;

    /// The index of refraction across the surface from \c v over that on its side, not 1.
    float eta;

    /// Whether a normal reflects by its Fresnel reflectance, or only by total internal reflection.
    bool splitByFresnel;

    /// \p x is any tangent that is not parallel to \p n.
    static GGXRefractionSampler make(
        float3 n, float3 x, float2 alpha, float eta, bool splitByFresnel)
    {
        return {GGXReflectionSampler.make(n, x, alpha), eta, splitByFresnel};
    }

    /// The probability that a visible normal reflects \c v, given its \p vDotH.
    float reflectionProbability(float vDotH)
    {
        let fresnel = mx_fresnel_dielectric(vDotH, eta);
        if (splitByFresnel)
            return fresnel;
        return fresnel >= 1.0 ? 1.0 : 0.0;
    }

    RefractionGeometry refraction(float3 v, float3 wi)
    {
        let sum = v + eta * wi;
        let length2 = dot(sum, sum);
        if (length2 == 0.0)
            return {false, float3(0.0), 0.0, 0.0, 0.0};

        let h = (dot(sum, visible.n) < 0.0 ? -1.0 : 1.0) * sum * rsqrt(length2);
        let vDotH = dot(v, h);
        let wiDotH = dot(wi, h);
        return {vDotH > 0.0 && wiDotH < 0.0, h, vDotH, wiDotH, eta * eta * -wiDotH / length2};
    }

    BSDFDirectionSample sample(float3 v, float u1, float2 u2)
    {
        let h = visible.sampleNormal(v, u2);
        if (u1 < reflectionProbability(dot(v, h)))
        {
            let wi = reflect(-v, h);
            if (dot(visible.n, wi) <= 0.0)
                return {float3(0.0, 0.0, 1.0), 1.0, BSDFLobe.None};
            return {wi, 1.0, BSDFLobe.GlossyReflection};
        }

        let wi = refract(-v, h, 1.0 / eta);
        if (dot(visible.n, wi) >= 0.0)
            return {float3(0.0, 0.0, 1.0), 1.0, BSDFLobe.None};
        return {wi, eta, BSDFLobe.GlossyTransmission};
    }

    /// The solid-angle density with which \c sample picks \p wi.
    float pdf(float3 v, float3 wi)
    {
        if (dot(visible.n, wi) > 0.0)
        {
            let h = normalize(v + wi);
            let vDotH = dot(v, h);
            return reflectionProbability(vDotH) * visible.normalPdf(v, h) / (4.0 * vDotH);
        }

        let geometry = refraction(v, wi);
        if (!geometry.valid)
            return 0.0;
        return (1.0 - reflectionProbability(geometry.vDotH)) *
               visible.normalPdf(v, geometry.h) * geometry.dHdWi;
    }
};
