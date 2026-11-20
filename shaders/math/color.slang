#ifndef RENDERLAB_COLOR_SLANG
#define RENDERLAB_COLOR_SLANG

float rec709Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float linearToSrgb(float value)
{
    return value <= 0.0031308 ? 12.92 * value : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

#endif
