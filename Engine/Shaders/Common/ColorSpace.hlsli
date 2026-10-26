#ifndef WP1_COLOR_SPACE
#define WP1_COLOR_SPACE
float3 DecodeSrgb(float3 value)
{
    return lerp(value/12.92,pow(max((value+.055)/1.055,0),2.4),step(.04045,value));
}
float3 EncodeSrgb(float3 value)
{
    value=max(value,0);
    return lerp(value*12.92,1.055*pow(value,1/2.4)-.055,step(.0031308,value));
}
#endif
