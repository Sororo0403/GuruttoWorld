// Material alpha is the authored transition control; the silhouette stays opaque.
#define PSMain TitleBasePSMain
#include "Mesh.hlsl"
#undef PSMain
float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 base = TitleBasePSMain(input);
    return float4(lerp(base.rgb, float3(1,1,1), 1-saturate(input.color.a)), 1);
}
