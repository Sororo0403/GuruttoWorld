cbuffer TriangleConstants : register(b0)
{
    float2 aspectScale;
    float rotationCos;
    float rotationSin;
};

struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    float3 rotatedPosition = float3(
        input.position.x * rotationCos + input.position.z * rotationSin,
        input.position.y,
        -input.position.x * rotationSin + input.position.z * rotationCos);
    // 正射影として、回転後の Z 座標 [-1, 1] を深度範囲 [0, 1] に変換します。
    output.position = float4(rotatedPosition.xy * aspectScale, rotatedPosition.z * 0.5f + 0.5f, 1.0f);
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return input.color;
}
