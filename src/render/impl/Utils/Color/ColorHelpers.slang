#ifndef RENDERLAB_COLOR_HELPERS_SLANG
#define RENDERLAB_COLOR_HELPERS_SLANG

float luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

float linearToSRGB(float lin)
{
    if (lin <= 0.0031308f)
        return lin * 12.92f;

    return pow(lin, 1.0f / 2.4f) * 1.055f - 0.055f;
}

float3 linearToSRGB(float3 lin)
{
    return float3(linearToSRGB(lin.x), linearToSRGB(lin.y), linearToSRGB(lin.z));
}

#endif
