cbuffer SphereConstants : register(b0)
{
    row_major float4x4 normalMatrix;
    row_major float4x4 worldViewProjection;
};

Texture2D<float4> sphereTexture : register(t0);
SamplerState textureSampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0f), worldViewProjection);
    output.normal = mul(float4(input.normal, 0.0f), normalMatrix).xyz;
    output.uv = input.uv;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 normal = normalize(input.normal);
    float3 toLight = normalize(float3(-0.4f, 0.7f, -1.0f));
    float lighting = 0.2f + 0.8f * saturate(dot(normal, toLight));
    float4 color = sphereTexture.Sample(textureSampler, input.uv);
    return float4(color.rgb * lighting, color.a);
}
