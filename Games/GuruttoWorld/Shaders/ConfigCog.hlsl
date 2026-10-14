// A dedicated effect control occupies flag bits 12..21; opacity and depth writes stay intact.
#define PSMain TitleBasePSMain
#include "Mesh.hlsl"
#undef PSMain
float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 base = TitleBasePSMain(input);
    return float4(lerp(base.rgb, float3(1,1,1), ((uint)lightingEnabled >> 12 & 1023) / 1023.0), 1);
}
