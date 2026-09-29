#include "Common/UvTransform.hlsli"

cbuffer SphereConstants : register(b0)
{
    row_major float3x3 normalMatrix;
    row_major float4x4 worldViewProjection;
    row_major float3x4 worldRows;
};

cbuffer LightingConstants : register(b1)
{
    float3 lightDirection;
    float lightIntensity;
    float3 lightColor;
    float ambientIntensity;
    float3 cameraPosition;
    float shininess;
    float specularStrength;
    float lightingEnabled;
};

Texture2D<float4> meshTexture : register(t0);
SamplerState textureSampler : register(s0);

struct VertexInput
{
    float4 color : COLOR;
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float4 color : COLOR;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0f), worldViewProjection);
    output.normal = mul(input.normal, normalMatrix);
    output.worldPosition = mul(worldRows, float4(input.position, 1.0f));
    output.color = input.color;
    output.uv = TransformUv(input.uv);
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 albedo = meshTexture.Sample(textureSampler, input.uv) * input.color;
    if (lightingEnabled < 0.5f)
    {
        return albedo;
    }
    float3 normal = normalize(input.normal);
    float directionLengthSquared = dot(lightDirection, lightDirection);
    float3 toLight = -lightDirection * rsqrt(max(directionLengthSquared, 0.00000001f));
    float diffuse = saturate(dot(normal, toLight));
    float specular = 0.0f;
    if (directionLengthSquared > 0.00000001f && diffuse > 0.0f)
    {
        float3 toCamera = cameraPosition - input.worldPosition;
        toCamera *= rsqrt(max(dot(toCamera, toCamera), 0.00000001f));
        float3 halfway = toLight + toCamera;
        halfway *= rsqrt(max(dot(halfway, halfway), 0.00000001f));
        specular = specularStrength * pow(saturate(dot(normal, halfway)), shininess);
    }
    float3 color = albedo.rgb * ambientIntensity;
    color += (albedo.rgb * diffuse + specular) * lightColor * lightIntensity;
    return float4(color, albedo.a);
}
