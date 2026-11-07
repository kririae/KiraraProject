import flux_bsdf;

public __target_intrinsic(cuda, "flux::OptixShadingContext") struct ShadingContext {};

public float3 fluxGetWorldNormal(ShadingContext ctx) { __intrinsic_asm "$0.normal"; }

public float3 fluxToShadingFrame(ShadingContext ctx, float3 v)
{
    return toShadingFrame(fluxGetWorldNormal(ctx), v);
}
