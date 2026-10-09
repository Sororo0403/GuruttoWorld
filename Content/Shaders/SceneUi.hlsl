#define UI_SCENE 1
#include "Ui.hlsl"

UiVertexOutput VSMain(float2 corner : POSITION)
{
    float2 local = (corner - 0.5f) * size;
    float2 rotated = float2(local.x * rotationCos - local.y * rotationSin,
        local.x * rotationSin + local.y * rotationCos);
    float2 pixel = position + size * 0.5f + rotated;
    UiVertexOutput output;
    output.position = mul(float4(pixel, 0, 1), canvasViewProjection);
    output.uv = TransformUv(lerp(uvRect.xy, uvRect.zw, corner));
    output.color = tint;
    output.canvasPosition = pixel;
    return output;
}
