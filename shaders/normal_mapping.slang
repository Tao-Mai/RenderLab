#ifndef RENDERLAB_NORMAL_MAPPING_SLANG
#define RENDERLAB_NORMAL_MAPPING_SLANG

float3 applyNormalMap(float3 normal, float4 tangent, float3 textureNormal, float scale)
{
    float3 N = normalize(normal);
    float3 T = normalize(tangent.xyz - N * dot(N, tangent.xyz));
    float3 B = cross(N, T) * tangent.w;
    float3 normalTs = textureNormal * 2.0 - 1.0;
    normalTs.xy *= scale;
    return normalize(T * normalTs.x + B * normalTs.y + N * normalTs.z);
}

#endif
