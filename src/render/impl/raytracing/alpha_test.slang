#ifndef RENDERLAB_RAY_TRACING_ALPHA_TEST_SLANG
#define RENDERLAB_RAY_TRACING_ALPHA_TEST_SLANG

#include "Scene/Material/StandardMaterialData.slang"
#include "raytracing/geometry.slang"
#include "Scene/Scene.slang"

bool failsAlphaCutoff(StandardMaterialData material, float2 uv)
{
    float alpha = sampleMaterialTexture(material.baseColorTexture, uv, material.baseColor).a;
    return alpha < material.alphaCutoff;
}

bool isMaskedTriangle(uint instanceIndex, uint primitiveIndex, float2 barycentrics)
{
    HitData instance = scene.objects[instanceIndex];
    StandardMaterialData material = materials[instance.materialIndex];
    if (material.alphaMode != kAlphaModeMask)
        return false;

    uint firstIndex = instance.firstIndex + primitiveIndex * 3;
    TriangleVertices triangle = loadTriangleVertices(instance.vertexAddress, instance.indexAddress, firstIndex);
    float2 uv = triangle.interpolate(barycentrics).uv;
    return failsAlphaCutoff(material, uv);
}

#endif
