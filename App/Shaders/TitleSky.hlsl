// スプライトの頂点変換を使い、画像に依存しない空を描画します。
#define PSMain SampleSpriteSurface
#include "Sprite.hlsl"
#undef PSMain
#include "Common/TitleAtmosphere.hlsli"

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float altitude = saturate((0.66f - input.uv.y) / 0.66f);
    float3 sky = lerp(TitleHorizonColor, TitleZenithColor, smoothstep(0.0f, 1.0f, altitude));
    return float4(sky, 1.0f);
}
