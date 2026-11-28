#ifndef RENDERLAB_SCENE_SLANG
#define RENDERLAB_SCENE_SLANG

#include "Scene/Camera/Camera.slang"
#include "Scene/HitInfo.slang"
#include "Scene/Lights/EnvMap.slang"
#include "Scene/Lights/LightCollection.slang"
#include "Scene/Lights/LightData.slang"
#include "Scene/Material/MaterialSystem.slang"
#include "Scene/SceneTypes.slang"
#include "Scene/VertexData.slang"

struct Scene
{
    RaytracingAccelerationStructure        rtAccel;

    StructuredBuffer<float4x4>             worldMatrices;
    StructuredBuffer<float4x4>             inverseTransposeWorldMatrices;

    StructuredBuffer<GeometryInstanceData> geometryInstances;
    StructuredBuffer<StaticVertexData>     vertices;
    StructuredBuffer<uint>                 indexData;

    ParameterBlock<MaterialSystem>         materials;

    uint                                   lightCount;
    StructuredBuffer<LightData>            lights;
    LightCollection                        lightCollection;
    EnvMap                                 envMap;
    Camera                                 camera;

    float4x4 loadWorldMatrix(const uint matrixID)
    {
        return worldMatrices[matrixID];
    }

    float3x3 loadInverseTransposeWorldMatrix(const uint matrixID)
    {
        return float3x3(inverseTransposeWorldMatrices[matrixID]);
    }

    float4x4 getWorldMatrix(const GeometryInstanceID instanceID)
    {
        const uint matrixID = geometryInstances[instanceID.index].globalMatrixID;

        return loadWorldMatrix(matrixID);
    }

    float3x3 getInverseTransposeWorldMatrix(const GeometryInstanceID instanceID)
    {
        const uint matrixID = geometryInstances[instanceID.index].globalMatrixID;

        return loadInverseTransposeWorldMatrix(matrixID);
    }

    bool isWorldMatrixFlippedWinding(const GeometryInstanceID instanceID)
    {
        return (geometryInstances[instanceID.index].flags & uint(GeometryInstanceFlags::TransformFlipped)) != 0;
    }

    bool isObjectFrontFaceCW(const GeometryInstanceID instanceID)
    {
        return (geometryInstances[instanceID.index].flags & uint(GeometryInstanceFlags::IsObjectFrontFaceCW)) != 0;
    }

    bool isWorldFrontFaceCW(const GeometryInstanceID instanceID)
    {
        return (geometryInstances[instanceID.index].flags & uint(GeometryInstanceFlags::IsWorldFrontFaceCW)) != 0;
    }

    GeometryInstanceData getGeometryInstance(const GeometryInstanceID instanceID)
    {
        return geometryInstances[instanceID.index];
    }

    uint getMaterialID(const GeometryInstanceID instanceID)
    {
        return geometryInstances[instanceID.index].materialID;
    }

    uint getLightCount()
    {
        return lightCount;
    }

    LightData getLight(uint lightIndex)
    {
        return lights[lightIndex];
    }

    uint3 getIndices(const GeometryInstanceID instanceID, const uint triangleIndex)
    {
        const GeometryInstanceData instance = getGeometryInstance(instanceID);
        const uint baseIndex = instance.ibOffset + triangleIndex * 3;
        uint3 vtxIndices = uint3(indexData[baseIndex], indexData[baseIndex + 1], indexData[baseIndex + 2]);
        vtxIndices += instance.vbOffset;

        return vtxIndices;
    }

    StaticVertexData getVertex(const uint index)
    {
        return vertices[index];
    }

    float3 getFaceNormalInObjectSpace(const StaticVertexData v[3], const bool isFrontFaceCW)
    {
        const float3 p0 = v[0].position;
        const float3 p1 = v[1].position;
        const float3 p2 = v[2].position;
        const float3 n = normalize(cross(p1 - p0, p2 - p0));

        return isFrontFaceCW ? -n : n;
    }

    void getVertexPositionsW(const GeometryInstanceID instanceID, const uint triangleIndex, out float3 p[3])
    {
        const uint3 vtxIndices = getIndices(instanceID, triangleIndex);
        const float4x4 worldMat = getWorldMatrix(instanceID);

        [unroll]
        for (int i = 0; i < 3; ++i)
        {
            p[i] = mul(worldMat, float4(vertices[vtxIndices[i]].position, 1.f)).xyz;
        }
    }

    void getVertexTexCoords(const GeometryInstanceID instanceID, const uint triangleIndex, out float2 texC[3])
    {
        const uint3 vtxIndices = getIndices(instanceID, triangleIndex);

        [unroll]
        for (int i = 0; i < 3; ++i)
        {
            texC[i] = vertices[vtxIndices[i]].texCrd;
        }
    }

    float3 computeFaceNormalAndAreaW(const GeometryInstanceID instanceID, const float3 p[3], out float triangleArea)
    {
        float3 n = cross(p[1] - p[0], p[2] - p[0]);
        const float doubleArea = length(n);
        triangleArea = 0.5f * doubleArea;
        if (isWorldFrontFaceCW(instanceID))
            n = -n;

        return doubleArea > 0.f ? n / doubleArea : float3(0.f);
    }

    float2 getVertexTexCoord(const GeometryInstanceID instanceID, const uint triangleIndex, const float2 barycentrics)
    {
        const uint3 vtxIndices = getIndices(instanceID, triangleIndex);
        const float3 weights = float3(1.f - barycentrics.x - barycentrics.y, barycentrics);

        return vertices[vtxIndices[0]].texCrd * weights.x
            + vertices[vtxIndices[1]].texCrd * weights.y
            + vertices[vtxIndices[2]].texCrd * weights.z;
    }

    VertexData computeVertexData(
        const StaticVertexData v[3],
        const float4x4 worldMat,
        const float3x3 worldInvTransposeMat,
        const bool isFrontFaceCW,
        const bool isTransformFlipped,
        const float3 barycentrics)
    {
        VertexData outV = {};

        outV.posW = v[0].position * barycentrics[0]
            + v[1].position * barycentrics[1]
            + v[2].position * barycentrics[2];

        outV.normalW = v[0].normal * barycentrics[0]
            + v[1].normal * barycentrics[1]
            + v[2].normal * barycentrics[2];

        outV.tangentW = v[0].tangent * barycentrics[0]
            + v[1].tangent * barycentrics[1]
            + v[2].tangent * barycentrics[2];

        outV.texC = v[0].texCrd * barycentrics[0]
            + v[1].texCrd * barycentrics[1]
            + v[2].texCrd * barycentrics[2];

        outV.faceNormalW = getFaceNormalInObjectSpace(v, isFrontFaceCW);

        outV.posW = mul(worldMat, float4(outV.posW, 1.f)).xyz;
        outV.normalW = normalize(mul(worldInvTransposeMat, outV.normalW));
        outV.faceNormalW = normalize(mul(worldInvTransposeMat, outV.faceNormalW));
        if (dot(outV.faceNormalW, outV.normalW) < 0.f)
            outV.faceNormalW = -outV.faceNormalW;

        const float3 tangent = mul(float3x3(worldMat), outV.tangentW.xyz);
        const float tangentLenSqr = dot(tangent, tangent);
        const float handedness = isTransformFlipped ? -1.f : 1.f;
        outV.tangentW = tangentLenSqr > 0.f
            ? float4(tangent * rsqrt(tangentLenSqr), sign(outV.tangentW.w) * handedness)
            : float4(0.f);

        return outV;
    }

    VertexData getVertexData(const GeometryInstanceID instanceID, const uint triangleIndex, const float3 barycentrics)
    {
        const uint3 vtxIndices = getIndices(instanceID, triangleIndex);
        const StaticVertexData v[3] = {
            getVertex(vtxIndices[0]),
            getVertex(vtxIndices[1]),
            getVertex(vtxIndices[2]),
        };

        const float4x4 worldMat = getWorldMatrix(instanceID);
        const float3x3 worldInvTransposeMat = getInverseTransposeWorldMatrix(instanceID);
        const bool isFrontFaceCW = isObjectFrontFaceCW(instanceID);
        const bool isTransformFlipped = isWorldMatrixFlippedWinding(instanceID);

        return computeVertexData(v, worldMat, worldInvTransposeMat, isFrontFaceCW, isTransformFlipped, barycentrics);
    }

    VertexData getVertexData(const TriangleHit hit)
    {
        return getVertexData(hit.instanceID, hit.primitiveIndex, hit.getBarycentricWeights());
    }
};

ParameterBlock<Scene> gScene;

#endif
