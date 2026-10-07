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
    float3 shadowCenter;
    float shadowRadius;
    float shadowBias;
    float shadowTexel;
};

Texture2D<float4> meshTexture : register(t0);
SamplerState textureSampler : register(s0);
Texture2D<float> shadowDepth : register(t1);
SamplerComparisonState shadowSampler : register(s1);
cbuffer ShadowTransform : register(b3)
{
    row_major float4x4 shadowWorldViewProjection;
};

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
    float3 cofactor1=cross(row2,row0);
    float3 cofactor2=cross(row0,row1);
    float3 largest=max(abs(cofactor0),max(abs(cofactor1),abs(cofactor2)));
    float scale=max(max(largest.x,largest.y),max(largest.z,1e-30f));
    // One uniform scale per object preserves interpolation while keeping large/small transforms in range.
    return (normal.x*(cofactor0/scale)+normal.y*(cofactor1/scale)+normal.z*(cofactor2/scale))*sign(dot(row0,cofactor0));
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

float4 VSShadow(float3 position : POSITION) : SV_POSITION
{
    return mul(float4(position,1),shadowWorldViewProjection);
}

float ShadowVisibility(float3 worldPosition,float3 normal)
{
    float visibility=1;
    [branch] if (shadowRadius>0)
    {
        float3 z=normalize(lightDirection);
        float3 reference=abs(z.y)>.95 ? float3(0,0,1) : float3(0,1,0);
        float3 x=normalize(cross(reference,z));
        float3 y=cross(z,x);
        float3 offset=worldPosition+normal*(shadowTexel*shadowRadius)-shadowCenter;
        float2 uv=float2(dot(offset,x),-dot(offset,y))/(2*shadowRadius)+.5;
        float depth=(dot(offset,z)+2*shadowRadius)/(4*shadowRadius)-shadowBias;
        // Compare each tap against the receiver plane at that tap, not a constant depth.
        float2 dx=ddx(uv),dy=ddy(uv);
        float dzdx=ddx(depth),dzdy=ddy(depth);
        float determinant=dx.x*dy.y-dx.y*dy.x;
        float2 gradient=0;
        if (abs(determinant)>1e-12)
            gradient=clamp(float2(dzdx*dy.y-dzdy*dx.y,dzdy*dx.x-dzdx*dy.x)/determinant,-4,4);
        float filterBias=(abs(gradient.x)+abs(gradient.y))*shadowTexel*.8;
        if (all(uv>=0) && all(uv<=1) && depth>0 && depth<1)
        {
            float amount=0;
            [unroll] for (int row=-1;row<=1;++row)
                [unroll] for (int column=-1;column<=1;++column)
                {
                    float2 tap=float2(column,row)*shadowTexel;
                    amount+=shadowDepth.SampleCmpLevelZero(shadowSampler,uv+tap,depth+dot(gradient,tap)-filterBias);
                }
            visibility=amount/9;
        }
    }
    return visibility;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 albedo = meshTexture.Sample(textureSampler, input.uv) * input.color;
    float3 color=albedo.rgb;
    if (lightingEnabled >= 0.5f)
    {
        float3 magnitude=abs(input.normal);
        float normalScale=max(max(magnitude.x,magnitude.y),max(magnitude.z,1e-30f));
        float3 normal = normalize(input.normal/normalScale);
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
        color += (albedo.rgb * diffuse + specular) * lightColor * lightIntensity * ShadowVisibility(input.worldPosition,normal);
    }
    float haze=smoothstep(fogStart,fogEnd,length(input.worldPosition-cameraPosition))*fogStrength;
    color=lerp(color,fogColor,haze);
    return float4(color, albedo.a);
}
