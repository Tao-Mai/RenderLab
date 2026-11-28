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
#ifndef RENDERLAB_STANDARD_MATERIAL_INSTANCE_SLANG
#define RENDERLAB_STANDARD_MATERIAL_INSTANCE_SLANG

#include "Rendering/Materials/BSDFs/StandardBSDF.slang"
#include "Rendering/Materials/IMaterialInstance.slang"

struct StandardMaterialInstance : IMaterialInstance
{
    ShadingFrame     sf;           // Shading frame in world space.
    StandardBSDFData data;         // BSDF parameters.
    float3           emission;     // Radiance emitted in the incident direction (wi).
    bool             thinSurface;

    __init(const ShadingFrame sf, const StandardBSDFData data, const float3 emission, const bool thinSurface)
    {
        this.sf = sf;
        this.data = data;
        this.emission = emission;
        this.thinSurface = thinSurface;
    }

    float3 eval<S : ISampleGenerator>(const ShadingData sd, const float3 wo, inout S sg)
    {
        float3 wiLocal = sf.toLocal(sd.V);
        float3 woLocal = sf.toLocal(wo);
        if (!isValidHemisphereReflectionOrTransmission(sd, sf, wiLocal, woLocal, wo))
            return float3(0.f);

        StandardBSDF bsdf = StandardBSDF(wiLocal, data, thinSurface);

        return bsdf.eval(wiLocal, woLocal);
    }

    bool sample<S : ISampleGenerator>(const ShadingData sd, inout S sg, out BSDFSample result)
    {
        result = {};
        float3 wiLocal = sf.toLocal(sd.V);
        float3 woLocal = {};

        StandardBSDF bsdf = StandardBSDF(wiLocal, data, thinSurface);
        if (!bsdf.sample(wiLocal, woLocal, result.pdf, result.weight, result.lobeType, sg))
            return false;

        result.wo = sf.fromLocal(woLocal);
        if (!isValidHemisphereReflectionOrTransmission(sd, sf, wiLocal, woLocal, result.wo))
            return false;

        return true;
    }

    float evalPdf(const ShadingData sd, const float3 wo)
    {
        float3 wiLocal = sf.toLocal(sd.V);
        float3 woLocal = sf.toLocal(wo);
        if (!isValidHemisphereReflectionOrTransmission(sd, sf, wiLocal, woLocal, wo))
            return 0.f;

        StandardBSDF bsdf = StandardBSDF(wiLocal, data, thinSurface);

        return bsdf.evalPdf(wiLocal, woLocal);
    }

    BSDFProperties getProperties(const ShadingData sd)
    {
        BSDFProperties p = {};
        p.guideNormal = sf.N;
        p.emission = emission;

        float alpha = data.roughness * data.roughness;
        p.roughness = alpha < kMinGGXAlpha ? 0.f : data.roughness;

        if (data.diffuseTransmission > 0.f || data.specularTransmission > 0.f)
            p.flags |= uint(BSDFProperties::Flags::IsTransmissive);

        return p;
    }

    uint getLobeTypes(const ShadingData sd)
    {
        return StandardBSDF::getLobeTypes(data);
    }
};

#endif
