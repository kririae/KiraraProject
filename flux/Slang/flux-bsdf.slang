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

/// \brief A direction that a BSDF's sampling picks, or a null event if the lobe is \c None.
public struct BSDFDirectionSample
{
    /// The shading-local direction toward the next path vertex.
    public float3 wi;

    /// The index of refraction on the side of \c wi divided by that on the side of \c wo.
    public float eta;

    public BSDFLobe lobe;
};

public struct BSDFResult
{
    public BSDFEvaluation evaluation;
    public BSDFSample sample;
};

/// \brief The density of \c sampleCosineHemisphere at \p cosine, the cosine to the normal.
public float cosineHemispherePdf(float cosine) { return cosine / 3.14159265; }

/// \brief A cosine-distributed direction around the unit \p n.
public float3 sampleCosineHemisphere(float3 n, float2 u)
{
    let t = abs(n.z) < 0.999 ? normalize(cross(float3(0.0, 0.0, 1.0), n))
                             : normalize(cross(float3(0.0, 1.0, 0.0), n));
    let b = cross(n, t);
    let r = sqrt(u.x);
    let phi = 6.2831853 * u.y;
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - u.x, 0.0)));
}

/// \brief Directions are shading-local and normalized, and \p u1 and \p u2 lie in [0, 1).
public interface IBSDF
{
    /// The value times the cosine, and the density with which \c sampleDirection picks \p wi.
    BSDFEvaluation evalAndPdf(float3 wo, float3 wi);

    BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2);
};

/// \brief Scatters nothing.
public struct EmptyBSDF : IBSDF
{
    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi) { return {float3(0.0), 0.0}; }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        return {float3(0.0, 0.0, 1.0), 1.0, BSDFLobe.None};
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

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        if (u1 < mix)
            return fg.sampleDirection(wo, u1 / mix, u2);
        return bg.sampleDirection(wo, (u1 - mix) / (1.0 - mix), u2);
    }
};

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

/// \brief Samples a direction of \p bsdf, and evaluates \p bsdf there once.
public BSDFSample sample<B : IBSDF>(B bsdf, float3 wo, float u1, float2 u2)
{
    let direction = bsdf.sampleDirection(wo, u1, u2);
    BSDFSample result = {float3(0.0), direction.wi, 0.0, direction.eta, direction.lobe};
    if (direction.lobe == BSDFLobe.None)
        return result;

    let evaluation = bsdf.evalAndPdf(wo, direction.wi);
    result.pdf = evaluation.pdf;
    if (evaluation.pdf > 0.0)
        result.weight = evaluation.value / evaluation.pdf;
    return result;
}

/// \brief Samples \p bsdf, and evaluates \p wi when \p eval is true.
///
/// \pre The directions are normalized and shading-local, and \p u1 and \p u2 lie in [0, 1).
public BSDFResult execute<B : IBSDF>(B bsdf, float3 wo, float3 wi, bool eval, float u1, float2 u2)
{
    BSDFResult result = {};
    if (eval)
        result.evaluation = bsdf.evalAndPdf(wo, wi);
    result.sample = sample(bsdf, wo, u1, u2);
    return result;
}
