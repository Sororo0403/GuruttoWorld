cbuffer UvConstants : register(b2)
{
    float4 uvScaleRotationOffsetX;
    float uvOffsetY;
};
float2 TransformUv(float2 uv)
{
    float sine,cosine; sincos(uvScaleRotationOffsetX.z,sine,cosine);
    float2 scaled=uv*uvScaleRotationOffsetX.xy;
    return float2(scaled.x*cosine-scaled.y*sine,scaled.x*sine+scaled.y*cosine)+float2(uvScaleRotationOffsetX.w,uvOffsetY);
}

cbuffer MeshConstants : register(b0)
{
    row_major float4x4 worldViewProjection;
    row_major float3x4 worldRows;
    float4 materialColor;
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
Texture2D<float4> normalTexture : register(t2);
StructuredBuffer<float4> localLights : register(t3);
struct SkinMatrix { row_major float4x4 position; row_major float4x4 normal; };
StructuredBuffer<SkinMatrix> skinPalette : register(t4);
StructuredBuffer<SkinMatrix> instances : register(t5);
Texture2DArray<float> localShadowDepth : register(t6);
Texture2D<float4> environmentTexture : register(t7);
Texture2D<float4> bakedLightmap : register(t8);
SamplerComparisonState shadowSampler : register(s1);
SamplerState environmentSampler : register(s2);
cbuffer ShadowTransform : register(b3)
{
    row_major float4x4 shadowWorldViewProjection;
};
cbuffer BoundsTransform : register(b4)
{
    row_major float4x4 boundsViewProjection;
    float4 boundsMinimum;
    float4 boundsMaximum;
};
float4 VSBounds(uint vertex : SV_VertexID) : SV_POSITION
{
    static const uint indices[36]={0,2,1,1,2,3,4,5,6,5,7,6,0,1,4,1,5,4,2,6,3,3,6,7,0,4,2,2,4,6,1,3,5,3,7,5};
    uint corner=indices[vertex];
    float3 position=lerp(boundsMinimum.xyz,boundsMaximum.xyz,float3(corner&1,(corner>>1)&1,(corner>>2)&1));
    return mul(float4(position,1),boundsViewProjection);
}

struct VertexInput
{
    float4 color : COLOR;
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float2 lightmapUv : TEXCOORD2;
    float4 color : COLOR;
};

float3 TransformNormal(float3 normal,float3 row0,float3 row1,float3 row2)
{
    float3 cofactor0=cross(row1,row2);
    float3 cofactor1=cross(row2,row0);
    float3 cofactor2=cross(row0,row1);
    float3 largest=max(abs(cofactor0),max(abs(cofactor1),abs(cofactor2)));
    float scale=max(max(largest.x,largest.y),max(largest.z,1e-30f));
    // One uniform scale per object preserves interpolation while keeping large/small transforms in range.
    return (normal.x*(cofactor0/scale)+normal.y*(cofactor1/scale)+normal.z*(cofactor2/scale))*sign(dot(row0,cofactor0));
}

float3 SkinPosition(float3 position,uint4 joints,float4 weights)
{
    float3 result=position;
    if (skinPalette[0].position[3][3]!=0)
    {
        float total=dot(weights,1);
        if (total<=0) result=mul(float4(position,1),skinPalette[0].position).xyz;
        else
        {
            result=0;
            [unroll] for (uint influence=0;influence<4;++influence)
                if (weights[influence]>0) result+=mul(float4(position,1),skinPalette[joints[influence]+1].position).xyz*weights[influence]/total;
        }
    }
    return result;
}
float3 SkinNormal(float3 normal,uint4 joints,float4 weights)
{
    float3 result=normal;
    if (skinPalette[0].position[3][3]!=0)
    {
        float total=dot(weights,1);
        if (total<=0) result=mul(float4(normal,0),skinPalette[0].normal).xyz;
        else
        {
            result=0;
            [unroll] for (uint influence=0;influence<4;++influence)
                if (weights[influence]>0) result+=mul(float4(normal,0),skinPalette[joints[influence]+1].normal).xyz*weights[influence];
        }
        float3 absolute=abs(result); float scale=max(max(absolute.x,absolute.y),max(absolute.z,1e-30));
        result/=scale; result*=rsqrt(max(dot(result,result),1e-20));
    }
    return result;
}
VertexOutput VSMain(VertexInput input,uint instanceId : SV_InstanceID)
{
    VertexOutput output;
    float3 position=SkinPosition(input.position,input.joints,input.weights);
    output.position = mul(float4(position, 1.0f), worldViewProjection);
    output.normal = TransformNormal(SkinNormal(input.normal,input.joints,input.weights),
        float3(worldRows[0].x,worldRows[1].x,worldRows[2].x),
        float3(worldRows[0].y,worldRows[1].y,worldRows[2].y),float3(worldRows[0].z,worldRows[1].z,worldRows[2].z));
    output.worldPosition = mul(worldRows, float4(position, 1.0f));
    if (((uint)lightingEnabled & 32)!=0)
    {
        SkinMatrix instance=instances[instanceId];
        output.position=mul(float4(position,1),instance.position);
        output.worldPosition=mul(float4(position,1),instance.normal).xyz;
        output.normal=TransformNormal(input.normal,instance.normal[0].xyz,instance.normal[1].xyz,instance.normal[2].xyz);
    }
    output.color = input.color*materialColor;
    output.uv = TransformUv(input.uv);
    output.lightmapUv=input.uv;
    return output;
}

float4 VSShadow(float3 position : POSITION,uint4 joints : BLENDINDICES,float4 weights : BLENDWEIGHT) : SV_POSITION
{
    return mul(float4(SkinPosition(position,joints,weights),1),shadowWorldViewProjection);
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

float3 SafeNormalize(float3 value)
{
    return value*rsqrt(max(dot(value,value),1e-20));
}
float LocalShadowVisibility(float3 position,float4 positionRange,float4 directionSpot,float4 cone)
{
    float visibility=1;
    [branch] if (cone.z>=0)
    {
        uint face=0;
        if (directionSpot.w<.5)
        {
            float3 direction=position-positionRange.xyz,magnitude=abs(direction);
            face=magnitude.x>=magnitude.y && magnitude.x>=magnitude.z ? (direction.x>=0 ? 0 : 1) :
                magnitude.y>=magnitude.z ? (direction.y>=0 ? 2 : 3) : (direction.z>=0 ? 4 : 5);
        }
        uint slice=(uint)cone.z+face;
        uint offset=129+slice*4;
        row_major float4x4 projection=float4x4(localLights[offset],localLights[offset+1],localLights[offset+2],localLights[offset+3]);
        float4 clip=mul(float4(position,1),projection);
        float3 ndc=clip.xyz/max(clip.w,1e-6);
        float2 uv=float2(ndc.x,-ndc.y)*.5+.5;
        if (clip.w>0 && all(uv>=0) && all(uv<=1) && ndc.z>=0 && ndc.z<=1)
        {
            visibility=0;
            [unroll] for (int y=-1;y<=1;++y)
                [unroll] for (int x=-1;x<=1;++x)
                    visibility+=localShadowDepth.SampleCmpLevelZero(shadowSampler,float3(uv+float2(x,y)/512,slice),ndc.z-.0005);
            visibility/=9;
        }
    }
    return visibility;
}

float3 SurfaceNormal(VertexOutput input,uint flags)
{
    float3 magnitude=abs(input.normal);
    float scale=max(max(magnitude.x,magnitude.y),max(magnitude.z,1e-30));
    float3 normal=SafeNormalize(input.normal/scale);
    // UVと位置の微分から接線を作るため、既存のOBJにも接線属性は不要です。
    float3 dx=ddx(input.worldPosition),dy=ddy(input.worldPosition);
    float2 ux=ddx(input.uv),uy=ddy(input.uv);
    float determinant=ux.x*uy.y-ux.y*uy.x;
    if ((flags & 2)!=0 && abs(determinant)>=1e-12)
    {
        float3 tangent=(dx*uy.y-dy*ux.y)/determinant;
        float3 bitangent=(dy*ux.x-dx*uy.x)/determinant;
        tangent-=normal*dot(normal,tangent);
        if (dot(tangent,tangent)>=1e-20)
        {
            tangent=SafeNormalize(tangent);
            float3 perpendicular=cross(normal,tangent);
            bitangent=perpendicular*(dot(perpendicular,bitangent)<0 ? -1 : 1);
            float3 mapped=normalTexture.Sample(textureSampler,input.uv).xyz*2-1;
            if ((flags & 4)!=0) mapped.y=-mapped.y;
            normal=SafeNormalize(tangent*mapped.x+bitangent*mapped.y+normal*mapped.z);
        }
    }
    return normal;
}

float3 SrgbToLinear(float3 value)
{
    return lerp(value/12.92,pow(max((value+.055)/1.055,0),2.4),step(.04045,value));
}
float3 LinearToSrgb(float3 value)
{
    value=max(value,0);
    return lerp(value*12.92,1.055*pow(value,1/2.4)-.055,step(.0031308,value));
}
float3 EnvironmentRadiance(float3 direction,float mip)
{
    direction=SafeNormalize(direction);
    float2 uv=float2(atan2(direction.z,direction.x)/6.28318530718+.5,acos(clamp(direction.y,-1,1))/3.14159265359);
    return SrgbToLinear(environmentTexture.SampleLevel(environmentSampler,uv,mip).rgb)*localLights[0].y;
}
float3 ImageBasedLighting(float3 baseColor,float3 normal,float3 toCamera,float roughness,float metallic)
{
    float3 reference=abs(normal.y)>.95 ? float3(0,0,1) : float3(0,1,0);
    float3 tangent=SafeNormalize(cross(reference,normal)),bitangent=cross(normal,tangent);
    float lastMip=max(localLights[0].z-1,0);
    float3 irradiance=EnvironmentRadiance(normal,lastMip)*.25;
    [unroll] for (uint index=0;index<8;++index)
    {
        float angle=(index+.5)*.78539816339;
        float3 direction=normal*.577350269+tangent*(cos(angle)*.816496581)+bitangent*(sin(angle)*.816496581);
        irradiance+=EnvironmentRadiance(direction,lastMip)*.09375;
    }
    float NoV=saturate(dot(normal,toCamera));
    float3 f0=lerp(.04,baseColor,metallic);
    float3 fresnel=f0+(max(1-roughness,f0)-f0)*pow(1-NoV,5);
    float3 reflected=EnvironmentRadiance(reflect(-toCamera,normal),roughness*lastMip);
    // Lazarovの環境BRDF近似。粗さと視線角から事前計算LUTを近似します。
    float4 coefficients=roughness*float4(-1,-.0275,-.572,.022)+float4(1,.0425,1.04,-.04);
    float a004=min(coefficients.x*coefficients.x,exp2(-9.28*NoV))*coefficients.x+coefficients.y;
    float2 brdf=float2(-1.04,1.04)*a004+coefficients.zw;
    return (1-fresnel)*(1-metallic)*baseColor*irradiance+reflected*(f0*brdf.x+brdf.y);
}

// GGX分布、Smithの高さ相関可視性、Schlick Fresnelによる金属度ワークフロー。
// https://google.github.io/filament/main/filament.html
float3 PbrLighting(float3 baseColor,float3 normal,float3 toLight,float3 toCamera,float roughness,float metallic)
{
    const float pi=3.14159265359;
    float3 halfway=SafeNormalize(toLight+toCamera);
    float NoL=saturate(dot(normal,toLight)),NoV=max(saturate(dot(normal,toCamera)),1e-5);
    float NoH=saturate(dot(normal,halfway)),VoH=saturate(dot(toCamera,halfway));
    float alpha=roughness*roughness,alphaSquared=alpha*alpha;
    float denominator=(1-NoH*NoH)+NoH*NoH*alphaSquared;
    float distribution=alphaSquared/(pi*max(denominator*denominator,1e-12));
    float visibility=.5/max(NoL*sqrt(NoV*NoV*(1-alphaSquared)+alphaSquared)+
        NoV*sqrt(NoL*NoL*(1-alphaSquared)+alphaSquared),1e-8);
    float3 f0=lerp(.04,baseColor,metallic);
    float3 fresnel=f0+(1-f0)*pow(1-VoH,5);
    float3 diffuse=(1-fresnel)*(1-metallic)*baseColor/pi;
    return (diffuse+distribution*visibility*fresnel)*NoL;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 sampled = meshTexture.Sample(textureSampler, input.uv);
    float4 albedo = sampled * input.color;
    float3 color=albedo.rgb;
    uint flags=(uint)lightingEnabled;
    bool pbr=shininess<0;
    bool hdr=(flags & 16)!=0;
    if ((flags & 1)!=0)
    {
        float3 normal=SurfaceNormal(input,flags);
        float directionLengthSquared = dot(lightDirection, lightDirection);
        float3 toLight = -lightDirection * rsqrt(max(directionLengthSquared, 0.00000001f));
        float diffuse = saturate(dot(normal, toLight));
        float specular = 0.0f;
        if (!pbr && directionLengthSquared > 0.00000001f && diffuse > 0.0f)
        {
            float3 toCamera = cameraPosition - input.worldPosition;
            toCamera *= rsqrt(max(dot(toCamera, toCamera), 0.00000001f));
            float3 halfway = toLight + toCamera;
            halfway *= rsqrt(max(dot(halfway, halfway), 0.00000001f));
            specular = specularStrength * pow(saturate(dot(normal, halfway)), shininess);
        }
        float visibility=ShadowVisibility(input.worldPosition,normal);
        if (pbr)
        {
            float3 baseColor=SrgbToLinear(sampled.rgb)*SrgbToLinear(input.color.rgb);
            color=baseColor*ambientIntensity;
            if (directionLengthSquared>1e-8)
                color+=PbrLighting(baseColor,normal,toLight,SafeNormalize(cameraPosition-input.worldPosition),
                    -shininess,specularStrength)*lightColor*lightIntensity*visibility;
        }
        else
        {
            color = albedo.rgb * ambientIntensity;
            color += (albedo.rgb * diffuse + specular) * lightColor * lightIntensity * visibility;
        }
        if ((flags & 8)!=0)
        {
            float3 baseColor=pbr ? SrgbToLinear(sampled.rgb)*SrgbToLinear(input.color.rgb) : albedo.rgb;
            float3 toCamera=SafeNormalize(cameraPosition-input.worldPosition);
            uint count=min((uint)localLights[0].x,32);
            [loop] for (uint index=0;index<count;++index)
            {
                uint offset=1+index*4;
                float4 positionRange=localLights[offset],directionSpot=localLights[offset+1];
                float4 radiance=localLights[offset+2],cone=localLights[offset+3];
                float3 difference=positionRange.xyz-input.worldPosition;
                float distanceSquared=dot(difference,difference);
                float normalizedDistanceSquared=distanceSquared/(positionRange.w*positionRange.w);
                float window=saturate(1-normalizedDistanceSquared*normalizedDistanceSquared);
                if (window<=0 || radiance.w<=0) continue;
                // 逆二乗の減衰を到達距離で滑らかに切り、光源の中心は半径1cmとして扱います。
                float attenuation=window*window/max(distanceSquared,1e-4);
                float3 direction=SafeNormalize(difference);
                if (directionSpot.w>.5)
                {
                    float angle=saturate((dot(SafeNormalize(directionSpot.xyz),-direction)-cone.y)/max(cone.x-cone.y,1e-6));
                    attenuation*=angle*angle;
                }
                float3 contribution;
                if (pbr) contribution=PbrLighting(baseColor,normal,direction,toCamera,-shininess,specularStrength);
                else
                {
                    float diffuse=saturate(dot(normal,direction));
                    float specular=diffuse>0 ? specularStrength*pow(saturate(dot(normal,SafeNormalize(direction+toCamera))),shininess) : 0;
                    contribution=baseColor*diffuse+specular;
                }
                color+=contribution*radiance.rgb*radiance.w*attenuation*LocalShadowVisibility(input.worldPosition,positionRange,directionSpot,cone);
            }
        }
        if ((flags & 128)!=0) color=0;
        if ((flags & 64)!=0)
        {
            float3 baseColor=SrgbToLinear(sampled.rgb)*SrgbToLinear(input.color.rgb);
            float3 contribution=ImageBasedLighting(baseColor,normal,SafeNormalize(cameraPosition-input.worldPosition),pbr ? -shininess : .5,pbr ? specularStrength : 0);
            color+=pbr ? contribution : LinearToSrgb(contribution);
        }
        if ((flags & 128)!=0)
        {
            float3 baseColor=pbr ? SrgbToLinear(sampled.rgb)*SrgbToLinear(input.color.rgb) : albedo.rgb;
            float3 baked=bakedLightmap.Sample(textureSampler,input.lightmapUv).rgb;
            color+=baseColor*(pbr ? SrgbToLinear(baked) : baked)*(pbr ? 1-specularStrength : 1);
        }
        if (pbr && !hdr) color=LinearToSrgb(color);
    }
    if (hdr && (!pbr || (flags & 1)==0)) color=SrgbToLinear(color);
    float haze=smoothstep(fogStart,fogEnd,length(input.worldPosition-cameraPosition))*fogStrength;
    color=lerp(color,hdr ? SrgbToLinear(fogColor) : fogColor,haze);
    return float4(color, albedo.a);
}
