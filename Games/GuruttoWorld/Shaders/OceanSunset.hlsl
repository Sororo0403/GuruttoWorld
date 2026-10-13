// Native fullscreen ray-generation pass. No sampled image assets.
#define PSMain SampleSpriteSurface
#include "Sprite.hlsl"
#undef PSMain
cbuffer OceanConstants : register(b1)
{
    float4 eyeTime;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraForward;
    float4 solar;
    float4 horizonHaze; // cloud base/thickness/extinction, aerosol density
    float4 zenithAmplitude; // ozone density, cloud anisotropy, reserved, wave amplitude
    float4 waterPeriod;
    float4 weather;
    float4 integration; // atmosphere, sunlight, clouds, cloud sunlight samples
    float4 intersection; // wave count, sea search, refinement, solar intensity
    float4 waveShape; // direction, spread, period ratio, amplitude ratio
    float4 cloudShape; // noise scale, wind X/Z, wave phase
    float4 sunlight; // RGB tint, sea trace distance
    float4 noiseWeights; // coarse/medium/fine, cloud trace distance
    float4 noiseFrequencies;
    float4 noiseOffsets;
};
static const float PI=3.141592654;
#include "Ocean/Atmosphere.hlsli"
#include "Ocean/Clouds.hlsli"

float3 EnvironmentRadiance(float3 origin,float3 direction)
{
    float3 sky=SkyRadiance(origin,direction,0);
    float3 disk=SunDiskRadiance(origin,direction);
    CloudResult cloud=TraceClouds(origin,direction);
    float enter=0,leave=0;
    float3 radiance=sky+disk;
    if(CloudSlab(origin,direction,enter,leave))
    {
        float3 air=AirTransmittance(origin,direction,enter);
        float3 foreground=SkyRadiance(origin,direction,enter);
        // Foreground air is unaffected by the cloud; farther sky is attenuated.
        radiance=foreground+(sky-foreground+disk)*cloud.transmittance+cloud.radiance*air;
    }
    return radiance;
}

#include "Ocean/Waves.hlsli"

struct EnvironmentOutput { float4 color : SV_TARGET; float depth : SV_DEPTH; };
EnvironmentOutput PSMain(VertexOutput input)
{
    float2 ndc=float2(input.uv.x*2-1,1-input.uv.y*2);
    float3 ray=normalize(cameraForward.xyz+cameraRight.xyz*ndc.x*cameraRight.w+cameraUp.xyz*ndc.y*cameraUp.w);
    SeaHit surface=IntersectSea(eyeTime.xyz,ray,sunlight.w);
    float3 color=surface.hit ? SeaRadiance(surface,ray) : EnvironmentRadiance(eyeTime.xyz,ray);
    EnvironmentOutput output;
    if(hdrOutput==0) color=EncodeSrgb(color/(1+color));
    output.color=float4(max(color,0),1);
    // Perspective depth participates in the engine's ordinary mesh depth test.
    float viewZ=surface.distance*dot(ray,cameraForward.xyz);
    output.depth=surface.hit && viewZ>0 ? saturate(tint.x+tint.y/viewZ) : 1;
    return output;
}
