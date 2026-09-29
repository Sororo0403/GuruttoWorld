cbuffer UvConstants : register(b2)
{
    float4 uvRow0;
    float4 uvRow1;
};

float2 TransformUv(float2 uv)
{
    float3 coordinate = float3(uv, 1.0f);
    return float2(dot(uvRow0.xyz, coordinate), dot(uvRow1.xyz, coordinate));
}
