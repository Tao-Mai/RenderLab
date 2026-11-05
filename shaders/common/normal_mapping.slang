#ifndef RENDERLAB_NORMAL_MAPPING_SLANG
#define RENDERLAB_NORMAL_MAPPING_SLANG

static const uint kNormalEncodingRgb = 0;
static const uint kNormalEncodingRgSnorm = 2;

float3 applyNormalMap(float3 normal, float4 tangent, float3 textureNormal, float scale, uint encoding)
{
    float3 N = normalize(normal);
    float3 T = normalize(tangent.xyz - N * dot(N, tangent.xyz));
    float3 B = cross(N, T) * tangent.w;
    float3 normalTs = textureNormal * 2.0 - 1.0;
    if (encoding != kNormalEncodingRgb)
    {
        float2 xy = encoding == kNormalEncodingRgSnorm ? textureNormal.xy : normalTs.xy;
        normalTs = float3(xy, sqrt(saturate(1.0 - dot(xy, xy))));
    }
    normalTs.xy *= scale;
    return normalize(T * normalTs.x + B * normalTs.y + N * normalTs.z);
}

#endif
