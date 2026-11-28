#ifndef RENDERLAB_MATERIAL_FACTORY_SLANG
#define RENDERLAB_MATERIAL_FACTORY_SLANG

#include "Rendering/Materials/StandardMaterial.slang"
#include "Scene/Material/MaterialSystem.slang"

extension MaterialSystem
{
    ShadingData prepareShadingData(const VertexData v, const uint materialID, const float3 viewDir)
    {
        ShadingData sd = {};
        sd.posW = v.posW;
        sd.uv = v.texC;
        sd.V = viewDir;

        bool valid;
        sd.frame = ShadingFrame::createSafe(v.normalW, v.tangentW, valid);
        sd.tangentW = valid ? v.tangentW : float4(sd.frame.T, 1.f);

        sd.faceN = v.faceNormalW;
        sd.frontFacing = dot(sd.V, sd.faceN) >= 0.f;

        sd.materialID = materialID;
        sd.IoR = 1.f;

        return sd;
    }

    StandardMaterialInstance getMaterialInstance(const ShadingData sd)
    {
        StandardMaterial material = { materialData[sd.materialID] };

        return material.setupMaterialInstance(this, sd);
    }
}

#endif
