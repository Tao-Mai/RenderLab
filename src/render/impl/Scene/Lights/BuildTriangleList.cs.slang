#include "Scene/Lights/LightCollectionShared.slang"
#include "Scene/Scene.slang"

struct BuildParameters
{
    uint gMaterialID;
    uint gInstanceID;
    uint gTriangleCount;
    uint gTriangleOffset;
};

ConstantBuffer<BuildParameters>            CB;
RWStructuredBuffer<PackedEmissiveTriangle> gTriangleData;

[shader("compute")]
[numthreads(256, 1, 1)]
void buildTriangleList(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= CB.gTriangleCount)
        return;

    const GeometryInstanceID instanceID = GeometryInstanceID(CB.gInstanceID);
    const uint triangleIndex = DTid.x;
    const uint triIdx = CB.gTriangleOffset + triangleIndex;

    EmissiveTriangle tri;
    gScene.getVertexPositionsW(instanceID, triangleIndex, tri.posW);
    gScene.getVertexTexCoords(instanceID, triangleIndex, tri.texCoords);
    tri.normal = gScene.computeFaceNormalAndAreaW(instanceID, tri.posW, tri.area);
    tri.materialID = CB.gMaterialID;

    gTriangleData[triIdx].pack(tri);
}
