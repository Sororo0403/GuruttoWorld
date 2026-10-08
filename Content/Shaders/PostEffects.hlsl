#include "Common/ColorSpace.hlsli"
cbuffer PostConstants : register(b0)
{
    float exposure,bloomIntensity,bloomThreshold,toneMapping;
    float2 texel;
    float passMode,outputHdr;
    float radius,bloomEnabled;
    float2 reserved;
};
Texture2D<float4> source : register(t0);
Texture2D<float4> bloom : register(t1);
SamplerState linearSampler : register(s0);
struct VertexOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VertexOutput VSMain(uint index : SV_VertexID)
{
    VertexOutput result;
    result.uv=float2((index<<1)&2,index&2);
    result.position=float4(result.uv.x*2-1,1-result.uv.y*2,0,1);
    return result;
}
float3 ReadColor(float2 uv)
{
    return clamp(source.Sample(linearSampler,uv).rgb,0,65504);
}
float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 color=ReadColor(input.uv);
    if (passMode==1)
    {
        color=(ReadColor(input.uv+texel*float2(-.5,-.5))+ReadColor(input.uv+texel*float2(.5,-.5))+
            ReadColor(input.uv+texel*float2(-.5,.5))+ReadColor(input.uv+texel*float2(.5,.5)))*.25;
        float brightness=max(color.r,max(color.g,color.b));
        color*=max(brightness-bloomThreshold,0)/max(brightness,1e-5);
    }
    else if (passMode>=2)
    {
        float2 stepSize=(passMode==2 ? float2(texel.x,0) : float2(0,texel.y))*radius;
        color*=.2270270270;
        const float weights[4]={.1945945946,.1216216216,.0540540541,.0162162162};
        [unroll] for (int index=1;index<=4;++index)
            color+=(ReadColor(input.uv+stepSize*index)+ReadColor(input.uv-stepSize*index))*weights[index-1];
    }
    else
    {
        if (bloomEnabled>.5) color+=clamp(bloom.Sample(linearSampler,input.uv).rgb,0,65504)*bloomIntensity;
        color*=exp2(exposure);
        if (toneMapping==1) color=color/(1+color);
        // NarkowiczのCC0公開曲線によるACESの近似。
        // https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
        else if (toneMapping==2) color=saturate((color*(2.51*color+.03))/(color*(2.43*color+.59)+.14));
        if (outputHdr<.5) color=EncodeSrgb(color);
    }
    return float4(color,passMode==0 ? source.Sample(linearSampler,input.uv).a : 1);
}
