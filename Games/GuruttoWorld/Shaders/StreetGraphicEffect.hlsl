// 元の素材による描画に、開始時の白黒シルエットだけを重ねます。
#define PSMain StreetBasePSMain
#include "Mesh.hlsl"
#undef PSMain

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 original = StreetBasePSMain(input);
    float silhouette = 0;
#if defined(STREET_SCAN_GROUND)
    silhouette = 1;
#endif
    float distanceFromOrigin = length(input.worldPosition - shaderParameters.yzw);
    float reveal = smoothstep(distanceFromOrigin - 0.45, distanceFromOrigin + 0.45, shaderParameters.x);
    return float4(lerp(silhouette.xxx, original.rgb, reveal), original.a);
}
