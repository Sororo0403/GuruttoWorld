// Xは開始からの秒数。主人公の周囲へ螺旋状に収束し、接線方向から街へほどけます。
#define VSMain GestureBaseVSMain
#define PSMain GestureBasePSMain
#include "Mesh.hlsl"
#undef VSMain
#undef PSMain
#include "Common/StreetArrivalWave.hlsli"

float WindRandom(float seed)
{
    return frac(sin(seed * 127.1 + 31.7) * 43758.5453);
}

VertexOutput VSMain(VertexInput input, uint instanceId : SV_InstanceID)
{
    float time = shaderParameters.x;
    float seed = input.uv.y;
    float along = input.position.x;
    float side = input.position.y;
    float dust = step(24, seed);
    float release = max(0, time - 0.38);
    float charge = saturate(time / 0.35);
    float unwind = smoothstep(0, 0.07, release);
    float inward = lerp(3.8, 0.72, charge * charge);
    float radius = max(inward, StreetWaveRadius(time));
    // 溜めで約一周巻き込み、放出後は回転を止めて同じ流れを外側へ伸ばします。
    float spin = min(time, 0.38) * 17 + unwind * 0.40;
    float angularSpan = lerp(0.50, 0, unwind) * (1 - dust);
    float radialSpan = lerp(0.16, lerp(0.55, 1.0, WindRandom(seed + 4)), unwind);
    radialSpan = lerp(radialSpan, 0.035, dust);
    float baseAngle = WindRandom(seed + 1) * 6.2831853;
    float3 target = float3(0, 0, 0);
    // 三本だけはひさしと左右の木へ向け、到達位置にも因果関係を持たせます。
    if (seed < 0.5) target = float3(2, 2.65, 7.1);
    else if (seed < 1.5) target = float3(11.8, 0.45, 5);
    else if (seed < 2.5) target = float3(-6.5, 0.45, 13);
    float targeted = 1 - step(2.5, seed);
    radius -= unwind * (1 - targeted) * lerp(0.2, 1.4, WindRandom(seed + 3));
    if (targeted > 0) baseAngle = atan2(target.z, target.x) - 6.86;
    float angle = baseAngle + spin + (along - 0.5) * angularSpan;
    float travel = max(0.4, radius + (along - 0.5) * radialSpan);
    float height = lerp(-0.35, 0.60, WindRandom(seed + 6));
    float elevation = lerp(0.02, 0.30, WindRandom(seed + 10));
    if (targeted > 0) elevation = normalize(target).y;
    float vertical = elevation * unwind;
    float3 direction = float3(cos(angle) * sqrt(1 - vertical * vertical), vertical, sin(angle) * sqrt(1 - vertical * vertical));
    float3 center = direction * travel + float3(0, height * (1 - unwind), 0);
    float3 tangent = normalize(direction * radialSpan + float3(-sin(angle), 0, cos(angle)) * travel * angularSpan);
    float3 origin = float3(worldRows[0].w, worldRows[1].w, worldRows[2].w);
    float3 toCamera = normalize(cameraPosition - origin - center);
    float3 ribbonSide = normalize(cross(tangent, toCamera) + float3(0.0001, 0, 0));
    float width = lerp(0.025, 0.055, WindRandom(seed + 7));
    width *= 1 + release;
    width = lerp(width, 0.012, dust);
    input.position = center + ribbonSide * side * width;
    VertexOutput output = GestureBaseVSMain(input, instanceId);
    output.uv = float2(along, side);
    float active = 1 - step(12, seed) * (1 - dust);
    output.color.a = lerp(0.24, 0.36, WindRandom(seed + 8)) * active;
    output.lightmapUv = float2(seed, dust);
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float time = shaderParameters.x;
    float envelope = smoothstep(0.025, 0.17, time) * (1 - smoothstep(0.58, 0.85, time));
    float taper = pow(saturate(sin(saturate(input.uv.x) * 3.1415927)), 1.5);
    float edge = 1 - smoothstep(0.20, 1, abs(input.uv.y));
    float gathering = 0.65 + 0.65 * smoothstep(0.18, 0.35, time);
    float alpha = envelope * taper * edge * gathering * input.color.a;
    clip(alpha - 0.002);
    float shade = lerp(0.52, 0.90, smoothstep(0.38, 0.52, time));
    return float4(shade.xxx, alpha);
}
