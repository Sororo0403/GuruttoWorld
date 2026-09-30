// World-space detail keeps the original CC0 palette UVs intact.
float SurfaceHash(float2 p)
{
    float3 q = frac(float3(p.xyx) * 0.1031f);
    q += dot(q, q.yzx + 33.33f);
    return frac((q.x + q.y) * q.z);
}

float SurfaceNoise(float2 p)
{
    float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(SurfaceHash(cell), SurfaceHash(cell + float2(1, 0)), f.x),
        lerp(SurfaceHash(cell + float2(0, 1)), SurfaceHash(cell + 1.0f), f.x), f.y);
}

float3 TitleSurfaceAlbedo(float3 albedo, float3 position, float3 normal)
{
    float3 n = abs(normal);
    float2 plane = n.y > 0.7f ? position.xz : (n.x > n.z ? position.zy : position.xy);
    float footprint = max(length(ddx(plane)), length(ddy(plane)));
    float fineFade = 1.0f - smoothstep(0.015f, 0.065f, footprint);
    float mediumFade = 1.0f - smoothstep(0.08f, 0.3f, footprint);
    float grain = (SurfaceNoise(plane * 34.0f) - 0.5f) * fineFade;
    float pores = (SurfaceNoise(plane * 6.0f) - 0.5f) * mediumFade;
    float weather = SurfaceNoise(plane * 0.65f) - 0.5f;
    float brightness = dot(albedo, float3(0.2126f, 0.7152f, 0.0722f));
    float chroma = max(albedo.r, max(albedo.g, albedo.b)) - min(albedo.r, min(albedo.g, albedo.b));
    float neutral = 1.0f - smoothstep(0.10f, 0.25f, chroma);
    float ground = step(0.7f, n.y) * (1.0f - step(0.3f, position.y));
    float wall = (1.0f - smoothstep(0.3f, 0.7f, n.y)) * smoothstep(0.35f, 0.65f, brightness);
    float amount = neutral * saturate(ground + wall);
    float detail = 1.0f + grain * lerp(0.14f, 0.28f, ground)
        + pores * 0.12f + weather * 0.10f;
    // Fine joints on the raised sidewalk only; leave lane markings untouched.
    float sidewalk = ground * step(0.12f, position.y) * step(0.55f, brightness);
    float2 edge = min(frac(plane / 0.8f), 1.0f - frac(plane / 0.8f)) * 0.8f;
    float aa = max(footprint, 0.001f);
    float joint = 1.0f - smoothstep(0.008f, 0.008f + aa, min(edge.x, edge.y));
    detail *= 1.0f - joint * sidewalk * mediumFade * 0.14f;
    // A restrained darker base suggests weathering, not baked cast shadows.
    float base = (1.0f - smoothstep(0.15f, 1.2f, position.y)) * wall;
    detail *= 1.0f - base * 0.09f;
    return albedo * lerp(1.0f, detail, amount);
}
