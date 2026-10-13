#ifndef OCEAN_ATMOSPHERE_INCLUDED
#define OCEAN_ATMOSPHERE_INCLUDED
// Independent numerical integration of participating-medium single scattering.
// Earth model constants follow Hillaire 2020 / Bruneton's published Earth model.
// Atmospheric calculations use kilometres; renderer/world inputs use metres.
static const float EarthRadius=6360.0;
static const float AtmosphereRadius=6460.0;
static const float3 RayleighCoefficient=float3(.005802,.013558,.033100); // 1/km
static const float3 OzoneCoefficient=float3(.000650,.001881,.000085); // 1/km
#define SolarIrradiance (sunlight.xyz*intersection.w)

float3 AtmospherePosition(float3 world)
{
    return float3(world.x*.001,EarthRadius+max(world.y-cameraForward.w,.5)*.001,world.z*.001);
}
float2 SphereInterval(float3 origin,float3 direction,float radius)
{
    float b=dot(origin,direction);
    // Difference of squares in this form keeps low-altitude intersections stable.
    float altitude=length(origin)-radius;
    float c=altitude*(length(origin)+radius);
    float discriminant=b*b-c;
    float2 interval=float2(-1,-1);
    if(discriminant>=0)
    {
        float root=sqrt(discriminant);
        interval=float2(-b-root,-b+root);
    }
    return interval;
}
float3 MediumDensity(float3 position)
{
    float altitude=max(length(position)-EarthRadius,0.0);
    return float3(exp(-altitude/8.0),exp(-altitude/1.2),max(0,1-abs(altitude-25.0)/15.0));
}
float3 Extinction(float3 density)
{
    return RayleighCoefficient*density.x+.004440*horizonHaze.w*density.y+
        OzoneCoefficient*density.z*zenithAmplitude.x;
}
float3 OpticalDepth(float3 origin,float3 direction,float distance)
{
    float3 tau=0;
    float previous=0;
    [loop] for(int i=0;i<(int)integration.y;++i)
    {
        float fraction=(i+1)/max(integration.y,1.0);
        float next=distance*fraction*fraction;
        float sampleDistance=(previous+next)*.5;
        tau+=Extinction(MediumDensity(origin+direction*sampleDistance))*(next-previous);
        previous=next;
    }
    return tau;
}
float3 SolarTransmittance(float3 atmosphereOrigin)
{
    float2 ground=SphereInterval(atmosphereOrigin,solar.xyz,EarthRadius);
    float2 air=SphereInterval(atmosphereOrigin,solar.xyz,AtmosphereRadius);
    float3 transmission=1;
    if(ground.x>0) transmission=0;
    else if(air.y>0) transmission=exp(-OpticalDepth(atmosphereOrigin,solar.xyz,air.y));
    return transmission;
}
float3 SolarAtPoint(float3 origin)
{
    return SolarIrradiance*SolarTransmittance(AtmospherePosition(origin));
}
float3 AirTransmittance(float3 origin,float3 direction,float distanceMetres)
{
    float3 transmission=1;
    if(distanceMetres>0) transmission=exp(-OpticalDepth(AtmospherePosition(origin),direction,distanceMetres*.001));
    return transmission;
}
float RayleighPhase(float mu) { return 3.0/(16.0*PI)*(1+mu*mu); }
float AerosolPhase(float mu)
{
    const float g=.8;
    return 3.0/(8.0*PI)*(1-g*g)/(2+g*g)*(1+mu*mu)/pow(max(1+g*g-2*g*mu,.001),1.5);
}
float3 SkyRadiance(float3 origin,float3 direction,float maxDistanceMetres)
{
    float3 position=AtmospherePosition(origin);
    float2 air=SphereInterval(position,direction,AtmosphereRadius);
    float distance=max(air.y,0);
    float2 ground=SphereInterval(position,direction,EarthRadius);
    if(ground.x>0) distance=min(distance,ground.x);
    if(maxDistanceMetres>0) distance=min(distance,maxDistanceMetres*.001);
    float mu=dot(direction,solar.xyz);
    float rayleigh=RayleighPhase(mu),mie=AerosolPhase(mu);
    float3 transmittance=1,radiance=0;
    float previous=0;
    [loop] for(int i=0;i<(int)integration.x;++i)
    {
        float fraction=(i+1)/max(integration.x,1.0);
        float next=distance*fraction*fraction;
        float stepLength=next-previous;
        float3 samplePosition=position+direction*((previous+next)*.5);
        float3 density=MediumDensity(samplePosition);
        float3 extinction=Extinction(density);
        float3 segmentT=exp(-extinction*stepLength);
        float3 scattering=RayleighCoefficient*density.x*rayleigh+.003996*horizonHaze.w*density.y*mie;
        float3 source=SolarIrradiance*SolarTransmittance(samplePosition)*scattering;
        radiance+=transmittance*source*(1-segmentT)/max(extinction,.000001);
        transmittance*=segmentT;
        previous=next;
    }
    return max(radiance,0);
}
float3 SunDiskRadiance(float3 origin,float3 direction)
{
    float angle=acos(clamp(dot(direction,solar.xyz),-1,1));
    float footprint=max(2*cameraUp.w/viewportSize.y,.00005);
    // Integrate the tiny disk's coverage over the angular pixel footprint.
    float coverage=1-smoothstep(max(0,solar.w-footprint),solar.w+footprint,angle);
    float solidAngle=2*PI*(1-cos(solar.w));
    return SolarAtPoint(origin)*coverage/max(solidAngle,.0000001);
}
#endif
