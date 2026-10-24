// 入口の既存メッシュだけを一度揺らし、衝突形状と着色後の素材を保持します。
// X: スキャン半径、Y: 演出の秒数、ZW: スキャン中心のXZ座標。
#define VSMain ArrivalBaseVSMain
#define PSMain ArrivalBasePSMain
#include "Mesh.hlsl"
#undef VSMain
#undef PSMain
#include "Common/StreetArrivalWave.hlsli"

float ArrivalImpulse(float time, float arrival)
{
    float elapsed = clamp(time - arrival, 0, 0.88);
    float attack = smoothstep(0, 0.055, elapsed);
    float finish = 1 - smoothstep(0.60, 0.88, elapsed);
    return sin(elapsed * 19) * exp(-elapsed * 4.2) * attack * finish;
}

void SwayCanopy(inout VertexInput input, float2 center, float direction)
{
    float2 offset = input.position.xz - center;
    if (abs(offset.x) > 0.60 || abs(offset.y) > 0.60 || input.position.y < 0.30 || input.position.y > 1.75) return;
    float3 origin = float3(shaderParameters.z, 0.95, shaderParameters.w);
    float arrival = StreetWaveArrival(length(float3(center.x, 1.40, center.y) - origin));
    float angle = ArrivalImpulse(shaderParameters.y, arrival) * 0.20 * direction;
    if (angle == 0) return;
    angle *= smoothstep(0.30, 1.40, input.position.y);
    float sine, cosine;
    sincos(angle, sine, cosine);
    float2 local = input.position.xy - float2(center.x, 0.29);
    input.position.xy = float2(local.x * cosine - local.y * sine, local.x * sine + local.y * cosine) + float2(center.x, 0.29);
    input.normal.xy = float2(input.normal.x * cosine - input.normal.y * sine, input.normal.x * sine + input.normal.y * cosine);
}

VertexOutput VSMain(VertexInput input, uint instanceId : SV_InstanceID)
{
    SwayCanopy(input, float2(-2.2, 5), -1);
    SwayCanopy(input, float2(-20.5, 13), 1);
    // ひさしの取付側を固定し、布の手前だけに小さな弾みを残します。
    if (input.position.x > -15.6 && input.position.x < -8.4 && input.position.z > 6.0 && input.position.z < 8.1 && input.position.y > 3.2 && input.position.y < 3.95)
    {
        float freeEdge = 1 - smoothstep(6.1, 7.9, input.position.z);
        float3 origin = float3(shaderParameters.z, 0.95, shaderParameters.w);
        float arrival = StreetWaveArrival(length(float3(-12, 3.60, 7.1) - origin));
        input.position.y += ArrivalImpulse(shaderParameters.y, arrival) * freeEdge * 0.20;
    }
    return ArrivalBaseVSMain(input, instanceId);
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 original = ArrivalBasePSMain(input);
    float3 origin = float3(shaderParameters.z, 1.05, shaderParameters.w);
    float distanceFromOrigin = length(input.worldPosition - origin);
    float reveal = smoothstep(distanceFromOrigin - 0.45, distanceFromOrigin + 0.45, shaderParameters.x);
    return float4(original.rgb * reveal, original.a);
}
