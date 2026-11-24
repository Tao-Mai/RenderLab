#ifndef RENDERLAB_RAY_TRACING_ALPHA_TEST_SLANG
#define RENDERLAB_RAY_TRACING_ALPHA_TEST_SLANG

#include "material/material_data.slang"
#include "raytracing/geometry.slang"
#include "scene/scene_data.slang"

bool failsAlphaCutoff(MaterialData material, float2 uv)
{
    float alpha = sampleTextureLevel(material.colorTexture, uv).a * material.opacityFactor;
    return alpha < material.alphaCutoff;
}

bool isMaskedTriangle(uint instanceIndex, uint primitiveIndex, float2 barycentrics)
{
    HitData instance = scene.objects[instanceIndex];
    MaterialData material = materials[instance.materialIndex];
    if (material.alphaMode != kAlphaModeMask)
        return false;

    uint firstIndex = instance.firstIndex + primitiveIndex * 3;
    TriangleVertices triangle = loadTriangleVertices(instance.vertexAddress, instance.indexAddress, firstIndex);
    float2 uv = triangle.interpolate(barycentrics).uv;
    return failsAlphaCutoff(material, uv);
}

#endif
