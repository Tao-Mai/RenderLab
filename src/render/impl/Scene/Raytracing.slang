#ifndef RENDERLAB_SCENE_RAYTRACING_SLANG
#define RENDERLAB_SCENE_RAYTRACING_SLANG

#include "Scene/Scene.slang"
#include "render/resources/RayType.h"

static const uint kSceneMask = 0x01;

void traceSceneRay<Payload>(RayDesc ray, RayType type, uint flags, inout Payload payload)
{
    const uint index = uint(type);
    // All instances and geometries share the same hit-group records.
    TraceRay(gScene.rtAccel, flags, kSceneMask, index, 0, index, ray, payload);
}

GeometryInstanceID getGeometryInstanceID()
{
    return GeometryInstanceID(InstanceID());
}

bool alphaTest(
    const GeometryInstanceID instanceID,
    const uint triangleIndex,
    const BuiltInTriangleIntersectionAttributes attribs)
{
    const uint materialID = gScene.getMaterialID(instanceID);
    if (gScene.materials.getMaterialData(materialID).alphaMode != kAlphaModeMask)
        return false;

    const float2 uv = gScene.getVertexTexCoord(instanceID, triangleIndex, attribs.barycentrics);

    return gScene.materials.alphaTest(uv, materialID);
}

#endif
