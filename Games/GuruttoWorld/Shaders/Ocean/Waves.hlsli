#ifndef OCEAN_WAVES_HLSLI
#define OCEAN_WAVES_HLSLI
// Linear deep-water gravity waves: omega^2 = g k (NVIDIA GPU Gems 1, ch. 1).
// Metres/seconds, analytic derivatives, no photograph or screen-space reflection.
// The nine directions, amplitude weights and pixel footprint are rendering choices,
// not measured weather. This bounded height field does not model breaking waves.
struct SeaHit { float distance; float3 position; float3 normal; bool hit; };

float SeaFootprint(float3 origin, float3 ray)
{
    float planeDistance=abs((origin.y-cameraForward.w)/max(abs(ray.y),1e-5));
    // Approximate one angular pixel projected onto the horizontal sea. Fix this
    // footprint throughout an intersection solve so its derivative stays analytic.
    float angularPixel=2.0*abs(cameraUp.w)/max(viewportSize.y,1.0);
    return min(4000.0,planeDistance*angularPixel/max(abs(ray.y),.025));
}
float3 SeaShape(float2 p,float footprint)
{
    float3 shape=0;
    [loop] for(int i=0;i<(int)intersection.x;++i)
    {
        float j=(float)i;
        float period=max(.2,waterPeriod.w)*pow(max(waveShape.z,0.001),j);
        float omega=6.283185307/period;
        float k=omega*omega/9.80665;
        float angle=waveShape.x+j*waveShape.y;
        float2 direction=float2(cos(angle),sin(angle));
        float amplitude=max(0,zenithAmplitude.w)*pow(max(waveShape.w,0.001),j)*exp(-k*footprint);
        float s,c; sincos(k*dot(direction,p)-omega*eyeTime.w+j*cloudShape.w,s,c);
        shape.x+=amplitude*s;
        shape.yz+=amplitude*k*c*direction;
    }
    return shape;
}
float2 SeaBounds(float footprint)
{
    float2 bounds=0;
    [loop] for(int i=0;i<(int)intersection.x;++i)
    {
        float j=(float)i;
        float omega=6.283185307/(max(.2,waterPeriod.w)*pow(max(waveShape.z,0.001),j));
        float k=omega*omega/9.80665;
        float a=max(0,zenithAmplitude.w)*pow(max(waveShape.w,0.001),j)*exp(-k*footprint);
        bounds+=float2(a,a*k);
    }
    return bounds; // absolute height and horizontal-gradient upper bounds
}
SeaHit IntersectSea(float3 origin,float3 direction,float maxDistance)
{
    SeaHit result;
    result.distance=0; result.position=origin; result.normal=float3(0,1,0); result.hit=false;
    if(maxDistance>0 && abs(direction.y)>=1e-7)
    {
        float footprint=SeaFootprint(origin,direction);
        float2 bounds=SeaBounds(footprint);
        if(bounds.x<1e-6)
        {
            float planeT=(cameraForward.w-origin.y)/direction.y;
            if(planeT>1e-4 && planeT<=maxDistance)
            { result.hit=true; result.distance=planeT; result.position=origin+direction*planeT; }
            // Amplitude zero is exactly the sea-level plane.
        }
        else
        {
            float a=(cameraForward.w+bounds.x-origin.y)/direction.y;
            float b=(cameraForward.w-bounds.x-origin.y)/direction.y;
            float t=max(0,min(a,b)),limit=min(maxDistance,max(a,b));
            if(limit>t)
            {
                // Conservative residual/Lipschitz advances cannot jump over a nearer crest.
                float lipschitz=abs(direction.y)+bounds.y*length(direction.xz);
                float initial=origin.y-cameraForward.w-SeaShape(origin.xz,footprint).x;
                float side=initial>=0 ? 1.0 : -1.0;
                bool marchFinished=false;
                // Below-surface origins find the exit interface with its upward normal.
                // SeaRadiance is above-water only; underwater absorption is out of scope.
                [loop] for(int iteration=0;iteration<(int)intersection.y;++iteration)
                {
                    float3 position=origin+direction*t;
                    float3 shape=SeaShape(position.xz,footprint);
                    float residual=position.y-cameraForward.w-shape.x;
                    float tolerance=max(.0008,t*.000003);
                    if(abs(residual)<=tolerance && t>1e-4)
                    {
                        result.hit=true; result.distance=t; result.position=position;
                        result.normal=normalize(float3(-shape.y,1,-shape.z));
                        marchFinished=true;
                        break;
                    }
                    if(residual*side<0) {marchFinished=true;break;}
                    t+=max(abs(residual)/max(lipschitz,1e-6),.0001);
                    if(t>limit) {marchFinished=true;break;}
                }
                if(!marchFinished)
                {
                    // Almost tangent rays converge slowly. Finish inside a sign
                    // bracket rather than permitting unbounded Newton steps.
                    float3 lowPosition=origin+direction*t;
                    float low=(lowPosition.y-cameraForward.w-SeaShape(lowPosition.xz,footprint).x)*side;
                    float3 highPosition=origin+direction*limit;
                    float high=(highPosition.y-cameraForward.w-SeaShape(highPosition.xz,footprint).x)*side;
                    if(low>=0 && high<=0)
                    {
                        float lo=t,hi=limit;
                        [loop] for(int refine=0;refine<(int)intersection.z;++refine)
                        {
                            t=lerp(lo,hi,clamp(low/max(low-high,1e-8),.15,.85));
                            float3 position=origin+direction*t;
                            float3 shape=SeaShape(position.xz,footprint);
                            float residual=(position.y-cameraForward.w-shape.x)*side;
                            result.distance=t; result.position=position;
                            result.normal=normalize(float3(-shape.y,1,-shape.z));
                            result.hit=true;
                            if(abs(residual)<=max(.0008,t*.000003)) break;
                            if(residual>0) {lo=t;low=residual;} else {hi=t;high=residual;}
                        }
                    }
                }
            }
        }
        // An above-water downward ray must cross the bounded height slab.
        // For unresolved wave detail only, a numerical tangent-solver failure
        // resolves to its mean plane. Require the entire slab within the clip
        // distance, so this approximation never resurrects a clipped interface.
        if(!result.hit && direction.y<0 && origin.y>cameraForward.w+bounds.x &&
            bounds.x<=footprint*.5)
        {
            float planeT=(cameraForward.w-origin.y)/direction.y;
            float farSlabT=(cameraForward.w-bounds.x-origin.y)/direction.y;
            if(planeT>1e-4 && farSlabT<=maxDistance)
            {
                result.hit=true; result.distance=planeT;
                result.position=origin+direction*planeT;
                result.normal=float3(0,1,0);
            }
        }
    }
    return result;
}
float WaterFresnel(float cosTheta)
{
    float c=saturate(cosTheta),eta=max(1.0001,weather.z);
    float ct=sqrt(max(0,1-(1-c*c)/(eta*eta)));
    float rs=(c-eta*ct)/max(c+eta*ct,1e-7);
    float rp=(eta*c-ct)/max(eta*c+ct,1e-7);
    return saturate(.5*(rs*rs+rp*rp)); // unpolarized dielectric Fresnel (PBRT 4e)
}
float3 SeaRadiance(SeaHit hit,float3 incomingRay)
{
    float3 reflection=reflect(incomingRay,hit.normal);
    float cosine=saturate(dot(-incomingRay,hit.normal));
    float fresnel=WaterFresnel(cosine);
    // Keep the true reflection direction even when it points below the horizon.
    // EnvironmentRadiance includes the finite solar disk: do not add a second sun.
    float3 reflected=EnvironmentRadiance(hit.position+hit.normal*.01,reflection);
    // RGB body albedo is an editable approximation for subsurface scattering,
    // lit by sky plus cloud-shadowed sunlight and weighted by transmitted energy.
    float3 diffuseSky=SkyRadiance(hit.position,float3(0,1,0),sunlight.w);
    float3 illumination=diffuseSky+SolarAtPoint(hit.position)*
        CloudSunTransmittance(hit.position)*max(dot(hit.normal,solar.xyz),0)/3.141592654;
    float3 body=max(waterPeriod.rgb,0)*illumination;
    float3 surface=fresnel*reflected+(1-fresnel)*body;
    return surface*AirTransmittance(eyeTime.xyz,incomingRay,hit.distance)+
        SkyRadiance(eyeTime.xyz,incomingRay,hit.distance);
}
#endif
