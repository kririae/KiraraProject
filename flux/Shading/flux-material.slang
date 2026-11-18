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

    /// The value times the cosine and the probability mass of a delta event, else zero.
    public BSDFEvaluation delta;
};

public bool isDelta(BSDFLobe lobe)
{
    return lobe == BSDFLobe.DeltaReflection || lobe == BSDFLobe.DeltaTransmission;
}

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

    /// The fraction of the energy from \p wo that the BSDF passes to the BSDF below it in a layer.
    float3 throughput(float3 wo);
};

/// \brief Scatters nothing.
public struct EmptyBSDF : IBSDF
{
    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi) { return {float3(0.0), 0.0}; }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        return {float3(0.0, 0.0, 1.0), 1.0, BSDFLobe.None};
    }

    public float3 throughput(float3 wo) { return float3(1.0); }
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
        {
            var sample = fg.sampleDirection(wo, u1 / mix, u2);
            sample.delta = {sample.delta.value * mix, sample.delta.pdf * mix};
            return sample;
        }
        var sample = bg.sampleDirection(wo, (u1 - mix) / (1.0 - mix), u2);
        sample.delta = {sample.delta.value * (1.0 - mix), sample.delta.pdf * (1.0 - mix)};
        return sample;
    }

    public float3 throughput(float3 wo) { return lerp(bg.throughput(wo), fg.throughput(wo), mix); }
};

public MixBSDF<A, B> makeMixBSDF<A : IBSDF, B : IBSDF>(A fg, B bg, float mix)
{
    return {fg, bg, mix};
}

/// \brief \c top over \c base, as MaterialX's \c layer, sampling \c top by the energy that it stops.
public struct LayerBSDF<Top : IBSDF, Base : IBSDF> : IBSDF
{
    public Top top;
    public Base base;

    static float topProbability(float3 topThroughput)
    {
        return 1.0 - (topThroughput.x + topThroughput.y + topThroughput.z) / 3.0;
    }

    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let et = top.evalAndPdf(wo, wi);
        let eb = base.evalAndPdf(wo, wi);
        let tt = top.throughput(wo);
        return {et.value + tt * eb.value, lerp(eb.pdf, et.pdf, topProbability(tt))};
    }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        let tt = top.throughput(wo);
        let p = topProbability(tt);
        if (u1 < p)
        {
            var sample = top.sampleDirection(wo, u1 / p, u2);
            sample.delta.pdf *= p;
            return sample;
        }
        var sample = base.sampleDirection(wo, (u1 - p) / (1.0 - p), u2);
        sample.delta = {sample.delta.value * tt, sample.delta.pdf * (1.0 - p)};
        return sample;
    }

    public float3 throughput(float3 wo) { return top.throughput(wo) * base.throughput(wo); }
};

public LayerBSDF<Top, Base> makeLayerBSDF<Top : IBSDF, Base : IBSDF>(Top top, Base base)
{
    return {top, base};
}

/// \brief \p bsdf with its value scaled by \c tint, as MaterialX's \c multiply.
public struct MultiplyBSDF<A : IBSDF> : IBSDF
{
    public A bsdf;
    /// The scale of the value, in [0, 1].
    public float3 tint;

    public BSDFEvaluation evalAndPdf(float3 wo, float3 wi)
    {
        let e = bsdf.evalAndPdf(wo, wi);
        return {e.value * tint, e.pdf};
    }

    public BSDFDirectionSample sampleDirection(float3 wo, float u1, float2 u2)
    {
        var sample = bsdf.sampleDirection(wo, u1, u2);
        sample.delta.value *= tint;
        return sample;
    }

    public float3 throughput(float3 wo) { return bsdf.throughput(wo); }
};

public MultiplyBSDF<A> makeMultiplyBSDF<A : IBSDF>(A bsdf, float3 tint)
{
    return {bsdf, clamp(tint, float3(0.0), float3(1.0))};
}

public MultiplyBSDF<A> makeMultiplyBSDF<A : IBSDF>(A bsdf, float tint)
{
    return makeMultiplyBSDF(bsdf, float3(tint));
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

    if (isDelta(direction.lobe))
    {
        result.pdf = direction.delta.pdf;
        if (result.pdf > 0.0)
            result.weight = direction.delta.value / result.pdf;
        return result;
    }

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

/// \brief Directions are shading-local and normalized.
public interface IEDF
{
    /// The radiance that the surface emits toward \p wo, on either side of the surface.
    float3 evaluate(float3 wo);
};

/// \brief Emits nothing.
public struct EmptyEDF : IEDF
{
    public float3 evaluate(float3 wo) { return float3(0.0); }
};

public struct UniformEDF : IEDF
{
    public float3 color;

    public float3 evaluate(float3 wo) { return color; }
};

public UniformEDF makeUniformEDF(float3 color) { return {color}; }

/// \brief \c base scaled by MaterialX's generalized Schlick factor of the angle to the normal.
public struct GeneralizedSchlickEDF<Base : IEDF> : IEDF
{
    public float3 color0;
    public float3 color90;
    public float exponent;
    public Base base;

    public float3 evaluate(float3 wo)
    {
        let cosine = clamp(abs(wo.z), 1e-8, 1.0);
        return base.evaluate(wo) * lerp(color0, color90, pow(1.0 - cosine, exponent));
    }
};

public GeneralizedSchlickEDF<Base> makeGeneralizedSchlickEDF<Base : IEDF>(
    float3 color0, float3 color90, float exponent, Base base)
{
    return {color0, color90, exponent, base};
}

public struct AddEDF<A : IEDF, B : IEDF> : IEDF
{
    public A in1;
    public B in2;

    public float3 evaluate(float3 wo) { return in1.evaluate(wo) + in2.evaluate(wo); }
};

public AddEDF<A, B> makeAddEDF<A : IEDF, B : IEDF>(A in1, B in2) { return {in1, in2}; }

public struct MixEDF<A : IEDF, B : IEDF> : IEDF
{
    public A fg;
    public B bg;
    /// The weight of \c fg, in [0, 1].
    public float mix;

    public float3 evaluate(float3 wo) { return lerp(bg.evaluate(wo), fg.evaluate(wo), mix); }
};

public MixEDF<A, B> makeMixEDF<A : IEDF, B : IEDF>(A fg, B bg, float mix)
{
    return {fg, bg, mix};
}

/// \brief \p edf with its value scaled by \c tint, as MaterialX's \c multiply.
public struct MultiplyEDF<A : IEDF> : IEDF
{
    public A edf;
    public float3 tint;

    public float3 evaluate(float3 wo) { return edf.evaluate(wo) * tint; }
};

public MultiplyEDF<A> makeMultiplyEDF<A : IEDF>(A edf, float3 tint) { return {edf, tint}; }

public MultiplyEDF<A> makeMultiplyEDF<A : IEDF>(A edf, float tint)
{
    return {edf, float3(tint)};
}

/// \brief A material, a BSDF and an EDF.
public struct Material<B : IBSDF, E : IEDF>
{
    public B bsdf;
    public E edf;
};

public Material<B, E> makeMaterial<B : IBSDF, E : IEDF>(B bsdf, E edf) { return {bsdf, edf}; }
