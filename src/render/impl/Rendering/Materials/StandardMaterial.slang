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

#include "Scene/Material/StandardMaterialData.slang"
#include "Rendering/Materials/StandardMaterialInstance.slang"

float getMetallic(float3 diffuse, float3 specular)
{
    float diffuseLuminance = rec709Luminance(diffuse);
    float specularLuminance = rec709Luminance(specular);
    if (specularLuminance == 0.0)
        return 0.0;

    float b = specularLuminance + diffuseLuminance - 0.08;
    float c = 0.04 - specularLuminance;

    return (sqrt(b * b - 0.16 * c) - b) * 12.5;
}

struct StandardMaterial
{
    StandardMaterialData data;

    StandardMaterialInstance setupMaterialInstance(float2 uv, float3 normal, bool frontFacing)
    {
        StandardMaterialInstance result = {};
        result.frame = ShadingFrame(normal);
        result.thinSurface = data.thinSurface != 0;
        result.data.eta = frontFacing ? 1.0 / data.indexOfRefraction : data.indexOfRefraction;
        result.data.diffuseTransmission = data.diffuseTransmission;
        result.data.specularTransmission = data.specularTransmission;
        if (data.diffuseTransmission > 0.0 || data.specularTransmission > 0.0)
            result.data.transmission = sampleMaterialTexture(data.transmissionTexture, uv, float4(data.transmissionColor, 0.0)).rgb;

        float fresnelRatio = (data.indexOfRefraction - 1.0) / (data.indexOfRefraction + 1.0);
        float3 baseColor = sampleMaterialTexture(data.baseColorTexture, uv, data.baseColor).rgb;
        StandardMaterialData sampled = data;
        sampled.specularParams = sampleMaterialTexture(data.specularTexture, uv, data.specularParams);
        if (data.shadingModel == kShadingModelMetalRough)
        {
            result.data.diffuse = lerp(baseColor, float3(0.0), sampled.metallic);
            result.data.specular = lerp(float3(fresnelRatio * fresnelRatio), baseColor, sampled.metallic);
            result.data.roughness = sampled.roughness;
            result.data.metallic = sampled.metallic;
        }
        else
        {
            result.data.diffuse = baseColor;
            result.data.specular = sampled.specularColor;
            result.data.roughness = 1.0 - sampled.glossiness;
            result.data.metallic = getMetallic(result.data.diffuse, result.data.specular);
        }
        result.emission = frontFacing ? evalMaterialEmission(data, uv) : float3(0.0);

        return result;
    }
};

#endif
