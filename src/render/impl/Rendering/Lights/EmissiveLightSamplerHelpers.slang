#ifndef RENDERLAB_EMISSIVE_LIGHT_SAMPLER_HELPERS_SLANG
#define RENDERLAB_EMISSIVE_LIGHT_SAMPLER_HELPERS_SLANG

#include "Scene/Lights/LightCollection.slang"
#include "Scene/Material/StandardMaterialData.slang"

float3 emissiveRadiance(uint triangleIndex, float2 uv, float3 outgoing)
{
    EmissiveTriangle triangle = emissiveLights.triangles[triangleIndex];
    StandardMaterialData material = materials[triangle.materialIndex];
    if (triangle.area == 0.0 ||
        dot(triangle.geometricNormal, outgoing) <= 0.0)
        return float3(0.0);
    return evalMaterialEmission(material, uv);
}

float3 sampleEmissiveTriangle(uint triangleIndex, float2 sampleValue, out float2 uv)
{
    EmissiveTriangle triangle = emissiveLights.triangles[triangleIndex];
    float root = sqrt(sampleValue.x);
    float3 barycentrics = float3(1.0 - root, root * (1.0 - sampleValue.y), root * sampleValue.y);
    uv = triangle.uv0 * barycentrics.x + triangle.uv1 * barycentrics.y + triangle.uv2 * barycentrics.z;
    return triangle.p0 * barycentrics.x + triangle.vertex1 * barycentrics.y + triangle.vertex2 * barycentrics.z;
}

#endif
