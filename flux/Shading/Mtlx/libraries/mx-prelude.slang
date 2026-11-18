#include "libraries/stdlib/genslang/lib/mx_texture.slang"
#define AIRY_FRESNEL_ITERATIONS 2
#define DIRECTIONAL_ALBEDO_METHOD 0

struct BSDF
{
    float3 response;
    float3 throughput;
};

#include "libraries/pbrlib/genglsl/lib/mx_closure_type.glsl"
#include "libraries/pbrlib/genglsl/lib/mx_environment_none.glsl"
