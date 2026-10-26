// エンジン内の全画面レイ生成パスです。画像アセットのサンプリングは行いません。
#define PSMain SampleSpriteSurface
#include "Sprite.hlsl"
#undef PSMain
cbuffer OceanConstants : register(b1)
{
    float4 eyeTime;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraForward;
    float4 solar;
    float4 horizonHaze; // 雲底・厚さ・消散係数、エアロゾル密度。
    float4 zenithAmplitude; // オゾン密度、雲の異方性、予約領域、波の振幅。
    float4 waterPeriod;
    float4 weather;
    float4 integration; // 大気・日光・雲・雲内の日光のサンプル数。
    float4 intersection; // 波の数、海面探索、精密化、太陽光の強さ。
    float4 waveShape; // 方向、広がり、周期比、振幅比。
    float4 cloudShape; // ノイズのスケール、風の X/Z、波の位相。
    float4 sunlight; // RGB の色調、海面の追跡距離。
    float4 noiseWeights; // 粗・中・細の設定、雲の追跡距離。
    float4 noiseFrequencies;
    float4 noiseOffsets;
};
static const float PI=3.141592654;
#include "Ocean/Atmosphere.hlsli"
#include "Ocean/Clouds.hlsli"

float3 EnvironmentRadiance(float3 origin,float3 direction)
{
    float3 sky=SkyRadiance(origin,direction,0);
    float3 disk=SunDiskRadiance(origin,direction);
    CloudResult cloud=TraceClouds(origin,direction);
    float enter=0,leave=0;
    float3 radiance=sky+disk;
    if(CloudSlab(origin,direction,enter,leave))
    {
        float3 air=AirTransmittance(origin,direction,enter);
        float3 foreground=SkyRadiance(origin,direction,enter);
        // 手前の大気は雲の影響を受けず、遠方の空は減衰します。
        radiance=foreground+(sky-foreground+disk)*cloud.transmittance+cloud.radiance*air;
    }
    return radiance;
}

#include "Ocean/Waves.hlsli"

struct EnvironmentOutput { float4 color : SV_TARGET; float depth : SV_DEPTH; };
EnvironmentOutput PSMain(VertexOutput input)
{
    float2 ndc=float2(input.uv.x*2-1,1-input.uv.y*2);
    float3 ray=normalize(cameraForward.xyz+cameraRight.xyz*ndc.x*cameraRight.w+cameraUp.xyz*ndc.y*cameraUp.w);
    SeaHit surface=IntersectSea(eyeTime.xyz,ray,sunlight.w);
    float3 color=surface.hit ? SeaRadiance(surface,ray) : EnvironmentRadiance(eyeTime.xyz,ray);
    EnvironmentOutput output;
    if(hdrOutput==0) color=EncodeSrgb(color/(1+color));
    output.color=float4(max(color,0),1);
    // 透視投影の深度を、エンジンの通常のメッシュ深度テストに使用します。
    float viewZ=surface.distance*dot(ray,cameraForward.xyz);
    output.depth=surface.hit && viewZ>0 ? saturate(tint.x+tint.y/viewZ) : 1;
    return output;
}
