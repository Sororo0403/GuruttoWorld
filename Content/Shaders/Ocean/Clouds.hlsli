#ifndef OCEAN_CLOUDS_INCLUDED
#define OCEAN_CLOUDS_INCLUDED

// Independent implementation of single-scattering volume transport.
// Beer-Lambert: pbr-book.org/4ed/Volume_Scattering/Transmittance
// HG phase: pbr-book.org/4ed/Volume_Scattering/Phase_Functions
// All positions and integration lengths are metres. The procedural weather
// field is an artistic cloud distribution, not a measured meteorological map.
struct CloudResult { float3 radiance; float transmittance; };

float CloudHash(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float CloudNoise(float3 p)
{
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(CloudHash(i), CloudHash(i + float3(1,0,0)), f.x),
    lerp(CloudHash(i + float3(0,1,0)), CloudHash(i + float3(1,1,0)), f.x), f.y),
    lerp(lerp(CloudHash(i + float3(0,0,1)), CloudHash(i + float3(1,0,1)), f.x),
    lerp(CloudHash(i + float3(0,1,1)), CloudHash(i + float3(1,1,1)), f.x), f.y), f.z);
}

float CloudDensity(float3 position, float footprint)
{
    float thickness = max(horizonHaze.y, 100.0);
    float h = (position.y - (cameraForward.w + horizonHaze.x)) / thickness;
    float result = 0.0;
    if (h > 0.0 && h < 1.0 && weather.x > 0.001)
    {
        float3 p = position + float3(eyeTime.w * weather.y * cloudShape.y, 0.0, eyeTime.w * weather.y * cloudShape.z);
        // Broad 3D masses with advected small-scale erosion. Filtering unresolved
        // octaves toward their mean prevents distant stair-stepped cloud skirts.
        p *= cloudShape.x;
        float coarse = CloudNoise(p * noiseFrequencies.x + noiseOffsets.x);
        float mediumWeight = 1.0 - smoothstep(350.0, 1200.0, footprint);
        float fineWeight = 1.0 - smoothstep(150.0, 550.0, footprint);
        float medium = lerp(0.5, CloudNoise(p * noiseFrequencies.y + noiseOffsets.y), mediumWeight);
        float fine = lerp(0.5, CloudNoise(p * noiseFrequencies.z + noiseOffsets.z), fineWeight);
        float field = coarse * noiseWeights.x + medium * noiseWeights.y + fine * noiseWeights.z;
        float threshold = lerp(0.76, 0.26, saturate(weather.x));
        float body = smoothstep(threshold, threshold + 0.19, field);
        float vertical = smoothstep(0.0, 0.16, h) * (1.0 - smoothstep(0.62, 1.0, h));
        result = body * vertical * max(weather.w, 0.0);
    }
    return result;
}

bool CloudSlab(float3 origin, float3 direction, out float enter, out float leave)
{
    float bottom = cameraForward.w + horizonHaze.x, top = bottom + max(horizonHaze.y, 100.0);
    enter = 0.0;
    leave = noiseWeights.w;
    bool result = origin.y > bottom && origin.y < top;
    if (abs(direction.y) >= 0.00001)
    {
        float a = (bottom - origin.y) / direction.y;
        float b = (top - origin.y) / direction.y;
        enter = max(0.0, min(a, b));
        leave = min(noiseWeights.w, max(a, b));
        result = leave > enter;
    }
    return result;
}

float CloudSunTransmittance(float3 origin)
{
    float result = 0.0;
    float enter = 0.0, leave = 0.0;
    if (solar.y > 0.0)
    {
        result = 1.0;
        if (CloudSlab(origin, solar.xyz, enter, leave))
        {
            // Four deterministic strata; a finite 60 km radius bounds grazing rays.
            float stepLength = (leave - enter) / max(integration.w,1.0);
            float opticalDepth = 0.0;
            [loop] for (int i = 0; i < (int)integration.w; ++i)
            {
                float3 p = origin + solar.xyz * (enter + (i + 0.5) * stepLength);
                opticalDepth += CloudDensity(p, stepLength * 0.2) * stepLength * max(horizonHaze.z, 0.0);
            }
            result = exp(-min(opticalDepth, 80.0));
        }
    }
    return result;
}

float CloudPhase(float cosineAngle)
{
    // View ray points into scene; light arrives along -sun. Forward scattering
    // is therefore strongest when the view ray points toward the sun.
    float g = clamp(zenithAmplitude.y, -0.9, 0.9);
    float denom = max(1.0 + g*g - 2.0*g*cosineAngle, 0.001);
    return (1.0 - g*g) / (4.0 * PI * denom * sqrt(denom));
}

CloudResult TraceClouds(float3 origin, float3 direction)
{
    CloudResult result;
    result.radiance = 0.0;
    result.transmittance = 1.0;
    float enter = 0.0, leave = 0.0;
    bool intersects = CloudSlab(origin, direction, enter, leave);
    if (weather.x > 0.001 && intersects)
    {
        float stepLength = (leave - enter) / max(integration.z,1.0);
        float3 reference = origin + direction * (enter + (leave - enter) * 0.35);
        float3 sunlight = SolarAtPoint(reference);
        // A single zenith sample is an approximate hemispherical ambient integral;
        // this does not claim to solve cloud multiple scattering or droplet Mie.
        float3 ambient = SkyRadiance(reference, float3(0,1,0), 0.0) * 0.35;
        float phase = CloudPhase(dot(direction, solar.xyz));
        [loop] for (int i = 0; i < (int)integration.z; ++i)
        {
            float distance = enter + (i + 0.5) * stepLength;
            float3 p = origin + direction * distance;
            float pixelFootprint = distance * 2.0 * abs(cameraUp.w) / max(viewportSize.y, 1.0);
            float density = CloudDensity(p, max(pixelFootprint, stepLength * 0.2));
            if (density > 0.0001)
            {
                float opticalDepth = density * max(horizonHaze.z, 0.0) * stepLength;
                float segmentT = exp(-min(opticalDepth, 80.0));
                float3 incident = sunlight * CloudSunTransmittance(p) * phase + ambient;
                // Near-unity single-scattering albedo approximates visible water
                // droplets; exact integration for constant density within a step.
                result.radiance += result.transmittance * (1.0 - segmentT) * 0.999 * incident;
                result.transmittance *= segmentT;
                if (result.transmittance < 0.005) break;
            }
        }
    }
    return result;
}
#endif
