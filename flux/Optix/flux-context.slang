import flux_bsdf;

public __target_intrinsic(cuda, "flux::OptixShadingContext") struct ShadingContext {};

public float3 fluxGetWorldNormal(ShadingContext ctx) { __intrinsic_asm "$0.normal"; }

public float2 fluxGetTexCoord(ShadingContext ctx) { __intrinsic_asm "$0.uv"; }

public float3 fluxToShadingFrame(ShadingContext ctx, float3 v)
{
    return toShadingFrame(fluxGetWorldNormal(ctx), v);
}

/// The value of texture `slot` of the BSDF at `uv`, with the derivatives of the surface point.
public float4 fluxSampleTexture(ShadingContext ctx, uint slot, float2 uv)
{
    __intrinsic_asm "flux::sampleImageTexture($0, $1, $2)";
}
