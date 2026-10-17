// 専用の効果制御にはフラグのビット 12～21 を使い、不透明度と深度の書き込みは維持します。
#define PSMain TitleBasePSMain
#include "Mesh.hlsl"
#undef PSMain
float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 base = TitleBasePSMain(input);
    return float4(lerp(base.rgb, float3(1,1,1), ((uint)lightingEnabled >> 12 & 1023) / 1023.0), 1);
}
