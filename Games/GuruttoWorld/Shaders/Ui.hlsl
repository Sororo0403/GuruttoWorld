#define VSMain SpriteVS
#define PSMain SpritePS
#include "Sprite.hlsl"
#undef VSMain
#undef PSMain

cbuffer UiCanvas : register(b1)
{
    row_major float4x4 canvasViewProjection;
    float4 clipRect;
};
struct UiVertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    float2 canvasPosition : TEXCOORD1;
};
#if !defined(UI_SCENE)
UiVertexOutput VSMain(float2 corner : POSITION)
{
    VertexOutput sprite=SpriteVS(corner);
    UiVertexOutput output;
    output.position=sprite.position;
    output.uv=sprite.uv;
    output.color=sprite.color;
    float2 local=(corner-.5f)*size;
    output.canvasPosition=position+size*.5f+float2(local.x*rotationCos-local.y*rotationSin,local.x*rotationSin+local.y*rotationCos);
    return output;
}
#endif
float4 PSMain(UiVertexOutput input) : SV_TARGET
{
    clip(input.canvasPosition-clipRect.xy);
    clip(clipRect.zw-input.canvasPosition);
    return FinishSpriteColor(spriteTexture.Sample(textureSampler,input.uv)*input.color);
}
