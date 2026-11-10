#include "mx-prelude.slang"
#include "libraries/pbrlib/genglsl/lib/mx_microfacet_specular.glsl"

/// \brief The alpha of \p roughness, floored at 1e-3 and capped at 1.
float2 makeGGXAlpha(float2 roughness) { return clamp(roughness, float2(1e-3), float2(1.0)); }

/// \brief Reflects a direction about a visible normal of a GGX distribution.
struct GGXReflectionSampler
{
    /// The unit normal on the side of \c v.
    float3 n;

    /// The unit tangent of \c alpha, orthogonal to \c n.
    float3 t;

    float2 alpha;

    /// \p x is any tangent that is not parallel to \p n.
    static GGXReflectionSampler make(float3 n, float3 x, float2 alpha)
    {
        return {n, normalize(x - dot(x, n) * n), alpha};
    }

    float3 toLocal(float3 v) { return float3(dot(v, t), dot(v, cross(n, t)), dot(v, n)); }

    /// A normal that \p v sees, picked with the density of \c normalPdf.
    float3 sampleNormal(float3 v, float2 u)
    {
        let h = mx_ggx_importance_sample_VNDF(u, toLocal(v), alpha);
        return t * h.x + cross(n, t) * h.y + n * h.z;
    }

    /// The density, over the solid angle of the normal, with which \c sampleNormal picks \p h.
    float normalPdf(float3 v, float3 h)
    {
        let nDotV = clamp(dot(n, v), M_FLOAT_EPS, 1.0);
        return mx_ggx_NDF(toLocal(h), alpha) * maskingG1(v) * dot(v, h) / nDotV;
    }

    float3 sample(float3 v, float2 u) { return reflect(-v, sampleNormal(v, u)); }

    /// The solid-angle density with which \c sample picks \p wi.
    float pdf(float3 v, float3 wi)
    {
        if (dot(n, wi) <= 0.0)
            return 0.0;

        let nDotV = clamp(dot(n, v), M_FLOAT_EPS, 1.0);
        let h = toLocal(normalize(v + wi));
        return mx_ggx_VNDF_reflection_PDF(h, alpha, maskingG1(v), nDotV);
    }

    /// Smith's G1 of \p v, with the alpha projected on the azimuth of \p v.
    float maskingG1(float3 v)
    {
        let lv = toLocal(v);
        let nDotV = clamp(lv.z, M_FLOAT_EPS, 1.0);
        let projected = alpha * lv.xy;
        let alphaV = sqrt(dot(projected, projected) / max(dot(lv.xy, lv.xy), 1e-12));
        return mx_ggx_smith_G1(nDotV, alphaV);
    }
};
