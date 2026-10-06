// スプライトの頂点変換を使い、画像に依存しない空を描画します。
#define PSMain SampleSpriteSurface
#include "Sprite.hlsl"
#undef PSMain
#include "Common/TitleAtmosphere.hlsli"

float CloudPuff(float2 uv, float2 center, float2 radius)
{
    float distance = length((uv - center) / radius);
    return 1.0f - smoothstep(0.65f, 1.05f, distance);
}

float CloudBank(float2 uv, float2 center, float size)
{
    // 水平の雲底に大小の丸い塊を重ね、輪郭を柔らかくします。
    float cloud = CloudPuff(uv, center, float2(0.105f, 0.024f) * size);
    cloud = max(cloud, CloudPuff(uv, center + float2(-0.055f, -0.012f) * size,
        float2(0.044f, 0.034f) * size));
    cloud = max(cloud, CloudPuff(uv, center + float2(-0.008f, -0.032f) * size,
        float2(0.055f, 0.047f) * size));
    cloud = max(cloud, CloudPuff(uv, center + float2(0.050f, -0.016f) * size,
        float2(0.043f, 0.034f) * size));
    return cloud;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float altitude = saturate((0.66f - input.uv.y) / 0.66f);
    float3 sky = lerp(TitleHorizonColor, TitleZenithColor, smoothstep(0.0f, 1.0f, altitude));
    // 建物の上に広い暖色の光を置き、輪郭が読める明るい空にします。
    float2 sunDistance = (input.uv - float2(0.78f, 0.13f)) / float2(0.22f, 0.27f);
    float sunlight = exp(-dot(sunDistance, sunDistance) * 2.0f);
    sky = lerp(sky, float3(1.0f, 0.95f, 0.79f), sunlight * 0.42f);
    float clouds = CloudBank(input.uv, float2(0.20f, 0.24f), 1.05f);
    clouds = max(clouds, CloudBank(input.uv, float2(0.51f, 0.12f), 0.85f));
    clouds = max(clouds, CloudBank(input.uv, float2(0.90f, 0.31f), 1.20f));
    float3 cloudColor = lerp(float3(0.88f, 0.92f, 0.94f), float3(1.0f, 0.98f, 0.91f), altitude);
    sky = lerp(sky, cloudColor, clouds * 0.86f);
    return float4(sky, 1.0f);
}
