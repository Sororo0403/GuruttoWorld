// スプライトの頂点変換を使い、画像に依存しない空を描画します。
#define PSMain SampleSpriteSurface
#include "Sprite.hlsl"
#undef PSMain
cbuffer SkyConstants : register(b1)
{
    float4 horizon;
    float4 zenith;
    float4 sunColor;
    float4 sunGeometry;
    float4 cloudLow;
    float4 cloudHigh;
    float4 cloudBanks[3];
};

float CloudPuff(float2 uv, float2 center, float2 radius)
{
    float distance = length((uv - center) / radius);
    return 1.0f - smoothstep(0.65f, 1.05f, distance);
}

float CloudBank(float2 uv, float2 center, float size)
{
    float safeSize=max(size,0.0001f);
    // 水平の雲底に大小の丸い塊を重ね、輪郭を柔らかくします。
    float cloud = CloudPuff(uv, center, float2(0.105f, 0.024f) * safeSize);
    cloud = max(cloud, CloudPuff(uv, center + float2(-0.055f, -0.012f) * safeSize,
        float2(0.044f, 0.034f) * safeSize));
    cloud = max(cloud, CloudPuff(uv, center + float2(-0.008f, -0.032f) * safeSize,
        float2(0.055f, 0.047f) * safeSize));
    cloud = max(cloud, CloudPuff(uv, center + float2(0.050f, -0.016f) * safeSize,
        float2(0.043f, 0.034f) * safeSize));
    return size>0.0f ? cloud : 0.0f;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float altitude = saturate((horizon.w - input.uv.y) / horizon.w);
    float3 sky = lerp(horizon.rgb, zenith.rgb, smoothstep(0.0f, 1.0f, altitude));
    // 建物の上に広い暖色の光を置き、輪郭が読める明るい空にします。
    float2 sunDistance = (input.uv - sunGeometry.xy) / sunGeometry.zw;
    float sunlight = exp(-dot(sunDistance, sunDistance) * 2.0f);
    sky = lerp(sky, sunColor.rgb, sunlight * sunColor.w);
    float clouds = CloudBank(input.uv, cloudBanks[0].xy, cloudBanks[0].z);
    clouds = max(clouds, CloudBank(input.uv, cloudBanks[1].xy, cloudBanks[1].z));
    clouds = max(clouds, CloudBank(input.uv, cloudBanks[2].xy, cloudBanks[2].z));
    float3 cloudColor = lerp(cloudLow.rgb, cloudHigh.rgb, altitude);
    sky = lerp(sky, cloudColor, clouds * zenith.w);
    return float4(sky, 1.0f);
}
