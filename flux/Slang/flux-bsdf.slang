/// \brief Joint result of BSDF evaluation and PDF calculation.
public struct BSDFEvaluation
{
    /// The BSDF value multiplied by the shading cosine.
    public float3 value;

    /// The solid-angle density of the queried direction.
    public float pdf;
};

/// \brief The values of \c flux::BSDFLobe.
public enum BSDFLobe : uint
{
    None = 0,
    DiffuseReflection = 1,
    DiffuseTransmission = 2,
    GlossyReflection = 3,
    GlossyTransmission = 4,
    DeltaReflection = 5,
    DeltaTransmission = 6,
};

public struct BSDFSample
{
    /// The throughput multiplier of the sampled direction.
    public float3 weight;

    /// The shading-local direction toward the next path vertex.
    public float3 wi;

    /// The solid-angle density of the sampled direction, or the probability mass of a delta sample.
    public float pdf;

    /// The index of refraction on the side of \c wi divided by that on the side of \c wo.
    public float eta;

    public BSDFLobe lobe;
};

public struct BSDFResult
{
    public BSDFEvaluation evaluation;
    public BSDFSample sample;
};

/// \brief Directions are shading-local and normalized, and \p u1 and \p u2 lie in [0, 1).
public interface IBSDF
{
    BSDFEvaluation evalAndPdf(float3 wo, float3 wi);

    BSDFSample sample(float3 wo, float u1, float2 u2);
};

/// \brief Scatters nothing.
public struct EmptyBSDF : IBSDF
{
    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi) { return {float3(0.0), 0.0}; }

    public BSDFSample sample(float3 wo, float u1, float2 u2)
    {
        return {float3(0.0), float3(0.0, 0.0, 1.0), 1.0, 1.0, BSDFLobe.None};
    }
};

/// \brief MaterialX 1.39.5's \c mx_oren_nayar_diffuse.
public struct OrenNayarDiffuseBSDF : IBSDF
{
    /// The unit normal, in the frame of \p wo and \p wi.
    public float3 n;

    public float3 albedo;

    public float roughness;

    float3 facing(float3 wo) { return dot(n, wo) < 0.0 ? -n : n; }

    float factor(float3 nf, float3 wo, float3 wi)
    {
        let nDotV = clamp(dot(nf, wo), 1e-6, 1.0);
        let nDotL = clamp(dot(nf, wi), 1e-6, 1.0);
        let lDotV = clamp(dot(normalize(wi), normalize(wo)), 1e-6, 1.0);
        let s = lDotV - nDotL * nDotV;
        let stinv = s > 0.0 ? s / max(nDotL, nDotV) : 0.0;
        let sigma2 = roughness * roughness;
        let a = 1.0 - 0.5 * (sigma2 / (sigma2 + 0.33));
        let b = 0.45 * sigma2 / (sigma2 + 0.09);
        return a + b * stinv;
    }

    /// \brief Zero for a \p wi on the other side of the facing normal from \p wo.
    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let nf = facing(wo);
        let nDotL = clamp(dot(nf, wi), 0.0, 1.0);
        if (nDotL <= 0.0)
            return {float3(0.0), 0.0};
        return {albedo * factor(nf, wo, wi) * nDotL / 3.14159265, nDotL / 3.14159265};
    }

    public BSDFSample sample(float3 wo, float u1, float2 u2)
    {
        let nf = facing(wo);
        let t = abs(nf.z) < 0.999 ? normalize(cross(float3(0.0, 0.0, 1.0), nf))
                                  : normalize(cross(float3(0.0, 1.0, 0.0), nf));
        let b = cross(nf, t);
        let r = sqrt(u2.x);
        let phi = 6.2831853 * u2.y;
        let wi =
            normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + nf * sqrt(max(1.0 - u2.x, 0.0)));
        let e = evalAndPdf(wo, wi);
        let weight = e.pdf > 0.0 ? e.value / e.pdf : float3(0.0);
        return {weight, wi, e.pdf, 1.0, BSDFLobe.DiffuseReflection};
    }
};

/// \brief Two BSDFs, one of which each sample selects.
public struct MixBSDF<A : IBSDF, B : IBSDF> : IBSDF
{
    public A fg;
    public B bg;
    /// The weight of \c fg, in [0, 1].
    public float mix;

    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let ef = fg.evalAndPdf(wo, wi);
        let eb = bg.evalAndPdf(wo, wi);
        return {lerp(eb.value, ef.value, mix), lerp(eb.pdf, ef.pdf, mix)};
    }

    public BSDFSample sample(float3 wo, float u1, float2 u2)
    {
        BSDFSample child;
        if (u1 < mix)
            child = fg.sample(wo, u1 / mix, u2);
        else
            child = bg.sample(wo, (u1 - mix) / (1.0 - mix), u2);

        if (all(child.weight == float3(0.0)))
            return child;

        let ef = fg.evalAndPdf(wo, child.wi);
        let eb = bg.evalAndPdf(wo, child.wi);
        let density = mix * ef.pdf + (1.0 - mix) * eb.pdf;
        let value = lerp(eb.value, ef.value, mix);
        let weight = density > 0.0 ? value / density : float3(0.0);
        return {weight, child.wi, density, child.eta, child.lobe};
    }
};

public OrenNayarDiffuseBSDF makeOrenNayarDiffuseBSDF(float3 albedo, float roughness, float3 n)
{
    return {n, albedo, roughness};
}

public MixBSDF<A, B> makeMixBSDF<A : IBSDF, B : IBSDF>(A fg, B bg, float mix)
{
    return {fg, bg, mix};
}

/// \brief \p v in the shading frame around \p normal, whose basis is that of \c flux::Frame.
public float3 toShadingFrame(float3 normal, float3 v)
{
    let sign = asfloat((asuint(normal.z) & 0x80000000u) | asuint(1.0));
    let a = -1.0 / (sign + normal.z);
    let b = normal.x * normal.y * a;
    let tangent = float3(1.0 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    let bitangent = float3(b, sign + normal.y * normal.y * a, -normal.y);
    return float3(dot(v, tangent), dot(v, bitangent), dot(v, normal));
}

/// \brief Runs \p bsdf once, and evaluates \p wi when \p eval is true.
///
/// \pre The directions are normalized and shading-local, and \p u1 and \p u2 lie in [0, 1).
public BSDFResult execute<B : IBSDF>(B bsdf, float3 wo, float3 wi, bool eval, float u1, float2 u2)
{
    BSDFResult result = {};
    if (eval)
        result.evaluation = bsdf.evalAndPdf(wo, wi);
    result.sample = bsdf.sample(wo, u1, u2);
    return result;
}
