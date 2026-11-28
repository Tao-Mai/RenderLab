/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#ifndef RENDERLAB_STANDARD_MATERIAL_SLANG
#define RENDERLAB_STANDARD_MATERIAL_SLANG

#include "Rendering/Materials/StandardMaterialInstance.slang"
#include "Scene/Material/MaterialSystem.slang"
#include "Scene/Material/ShadingUtils.slang"
#include "Scene/Material/StandardMaterialData.slang"

struct StandardMaterial
{
    StandardMaterialData data;

    StandardMaterialInstance setupMaterialInstance(const MaterialSystem ms, const ShadingData sd)
    {
        StandardBSDFData d = {};
        d.eta = sd.frontFacing ? (sd.IoR / data.indexOfRefraction) : (data.indexOfRefraction / sd.IoR);
        d.diffuseTransmission = data.diffuseTransmission;
        d.specularTransmission = data.specularTransmission;
        if (d.diffuseTransmission > 0.f || d.specularTransmission > 0.f)
            d.transmission = ms.sampleTexture(data.transmissionTexture, sd.uv, float4(data.transmissionColor, 0.f)).rgb;

        float f = (data.indexOfRefraction - 1.f) / (data.indexOfRefraction + 1.f);
        float F0 = f * f;

        const float3 baseColor = ms.sampleTexture(data.baseColorTexture, sd.uv, data.baseColor).rgb;
        StandardMaterialData sampled = data;
        sampled.specularParams = ms.sampleTexture(data.specularTexture, sd.uv, data.specularParams);
        if (data.shadingModel == kShadingModelMetalRough)
        {
            d.diffuse = lerp(baseColor, float3(0.f), sampled.metallic);
            d.specular = lerp(float3(F0), baseColor, sampled.metallic);
            d.roughness = sampled.roughness;
            d.metallic = sampled.metallic;
        }
        else
        {
            d.diffuse = baseColor;
            d.specular = sampled.specularColor;
            d.roughness = 1.f - sampled.glossiness;
            d.metallic = getMetallic(d.diffuse, d.specular);
        }

        ShadingFrame sf = sd.frame;
        if (data.normalTexture.textureIndex != kInvalidMaterialTexture)
        {
            const float3 encodedNormal = ms.sampleTexture(data.normalTexture, sd.uv, float4(0.5f, 0.5f, 1.f, 1.f)).xyz;
            sf = computeShadingFrameFromNormalMap(sd, encodedNormal, data.normalScale, data.normalEncoding);
        }
        flipShadingNormal(sd, data.doubleSided != 0, sf);
        adjustShadingNormal(sd, sf);

        const float3 emission = sd.frontFacing
            ? ms.sampleTexture(data.emissiveTexture, sd.uv, float4(data.emissiveColor, 1.f)).rgb * data.emissiveFactor
            : float3(0.f);

        return StandardMaterialInstance(sf, d, emission, data.thinSurface != 0);
    }
};

#endif
