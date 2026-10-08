#define PSMain UnusedParticlePS
#include "Particle.hlsl"
#undef PSMain

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float2 center = input.uv * 2.0f - 1.0f;
    float glow = saturate(1.0f - dot(center, center));
    return FinishParticleColor(float4(input.color.rgb, input.color.a * glow * glow));
}
