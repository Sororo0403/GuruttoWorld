#define VSMain ScreenSpriteVS
#include "Sprite.hlsl"
#undef VSMain

cbuffer SceneCanvas : register(b1)
{
    row_major float4x4 canvasViewProjection;
};

VertexOutput VSMain(float2 corner : POSITION)
{
    float2 local = (corner - 0.5f) * size;
    float2 rotated = float2(local.x * rotationCos - local.y * rotationSin,
        local.x * rotationSin + local.y * rotationCos);
    float2 pixel = position + size * 0.5f + rotated;
    VertexOutput output;
    output.position = mul(float4(pixel, 0, 1), canvasViewProjection);
    output.uv = TransformUv(lerp(uvRect.xy, uvRect.zw, corner));
    output.color = tint;
    return output;
}
