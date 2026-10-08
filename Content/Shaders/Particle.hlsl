#include "Common/ColorSpace.hlsli"
cbuffer ParticleOutput : register(b1) { uint hdrOutput; };
float4 FinishParticleColor(float4 value)
{
    return float4(hdrOutput!=0 ? DecodeSrgb(value.rgb) : value.rgb,value.a);
}
cbuffer ParticleConstants : register(b0)
{
    row_major float4x4 worldViewProjection;
    float4 tint;
};
Texture2D<float4> particleTexture : register(t0);
SamplerState textureSampler : register(s0);
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
VertexOutput VSMain(float2 corner : POSITION)
{
    VertexOutput output;
    output.position = mul(float4(corner.x - 0.5f, 0.5f - corner.y, 0.0f, 1.0f), worldViewProjection);
    output.uv = corner;
    output.color = tint;
    return output;
}
float4 PSMain(VertexOutput input) : SV_TARGET
{
    return FinishParticleColor(particleTexture.Sample(textureSampler, input.uv) * input.color);
}
