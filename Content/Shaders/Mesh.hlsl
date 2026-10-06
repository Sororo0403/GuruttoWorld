#include "Common/UvTransform.hlsli"

cbuffer MeshConstants : register(b0)
{
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
    float fogStart;
    float fogEnd;
    float3 fogColor;
    float fogStrength;
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

float3 TransformNormal(float3 normal)
{
    float3 row0=float3(worldRows[0].x,worldRows[1].x,worldRows[2].x);
    float3 row1=float3(worldRows[0].y,worldRows[1].y,worldRows[2].y);
    float3 row2=float3(worldRows[0].z,worldRows[1].z,worldRows[2].z);
    float3 cofactor0=cross(row1,row2);
    // Inverse transpose up to a positive scale; PS normalizes it. Keep the determinant sign for mirrors.
    return (normal.x*cofactor0+normal.y*cross(row2,row0)+normal.z*cross(row0,row1))*sign(dot(row0,cofactor0));
}

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0f), worldViewProjection);
    output.normal = TransformNormal(input.normal);
    output.worldPosition = mul(worldRows, float4(input.position, 1.0f));
    output.color = input.color;
    output.uv = TransformUv(input.uv);
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 albedo = meshTexture.Sample(textureSampler, input.uv) * input.color;
    float3 color=albedo.rgb;
    if (lightingEnabled >= 0.5f)
    {
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
        color = albedo.rgb * ambientIntensity;
        color += (albedo.rgb * diffuse + specular) * lightColor * lightIntensity;
    }
    float haze=smoothstep(fogStart,fogEnd,length(input.worldPosition-cameraPosition))*fogStrength;
    color=lerp(color,fogColor,haze);
    return float4(color, albedo.a);
}
