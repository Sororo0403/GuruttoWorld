cbuffer SpriteConstants : register(b0)
{
    float2 viewportSize;
    float2 position;
    float2 size;
    float rotationCos;
    float rotationSin;
    float4 tint;
    float4 uvRect;
};

Texture2D<float4> spriteTexture : register(t0);
SamplerState textureSampler : register(s0);

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 corners[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1)
    };
    float2 corner = corners[vertexId];
    float2 local = (corner - 0.5f) * size;
    float2 rotated = float2(local.x * rotationCos - local.y * rotationSin,
        local.x * rotationSin + local.y * rotationCos);
    float2 pixelPosition = position + size * 0.5f + rotated;
    VertexOutput output;
    output.position = float4(pixelPosition.x / viewportSize.x * 2.0f - 1.0f,
        1.0f - pixelPosition.y / viewportSize.y * 2.0f, 0.0f, 1.0f);
    output.uv = lerp(uvRect.xy, uvRect.zw, corner);
    output.color = tint;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return spriteTexture.Sample(textureSampler, input.uv) * input.color;
}
