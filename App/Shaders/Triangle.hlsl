#include "UVTransform.hlsli"

cbuffer TriangleConstants : register(b0)
{
    float2 aspectScale;
    float rotationCos;
    float rotationSin;
    float3 translation;
    float padding;
    float4 tint;
};

Texture2D<float4> triangleTexture : register(t0);
SamplerState textureSampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    float3 rotatedPosition = float3(
        input.position.x * rotationCos + input.position.z * rotationSin,
        input.position.y,
        -input.position.x * rotationSin + input.position.z * rotationCos);
    rotatedPosition += translation;
    // 正射影として、回転後の Z 座標 [-1, 1] を深度範囲 [0, 1] に変換します。
    output.position = float4(rotatedPosition.xy * aspectScale, rotatedPosition.z * 0.5f + 0.5f, 1.0f);
    output.color = input.color * tint;
    output.uv = TransformUV(input.uv);
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return triangleTexture.Sample(textureSampler, input.uv) * input.color;
}
