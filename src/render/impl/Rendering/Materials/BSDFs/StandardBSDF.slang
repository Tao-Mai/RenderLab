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
#ifndef RENDERLAB_STANDARD_BSDF_SLANG
#define RENDERLAB_STANDARD_BSDF_SLANG

#include "Rendering/Materials/Fresnel.slang"
#include "Rendering/Materials/IsotropicGGX.slang"
#include "Utils/Sampling/SampleGeneratorInterface.slang"
#include "math/color.slang"

struct BSDFSample
{
    float3 direction;
    float3 weight; // Selected lobe's f * abs(NdotL), divided by its sampling probability.
    float pdf;     // Complete continuous mixture PDF; zero for delta events.
    bool valid;
    bool isDelta;
};

struct StandardBSDFData
{
    float3 diffuse;
    float3 specular;
    float roughness;
    float metallic;
    float eta;
    float3 transmission;
    float diffuseTransmission;
    float specularTransmission;
};

// All directions are local, with wi pointing toward the previous path vertex and N = +Z.
// eval() includes abs(NdotL), matching Falcor's IBSDF contract.
struct StandardBSDF
{
    StandardBSDFData data;
    float3 transmissionAlbedo;
    float alpha;
    float transmissionAlpha;
    float4 probabilities;

    property float pDiffuseReflection { get { return probabilities.x; } }
    property float pDiffuseTransmission { get { return probabilities.y; } }
    property float pSpecularReflection { get { return probabilities.z; } }
    property float pSpecularTransmission { get { return probabilities.w; } }

    __init(float3 wi, StandardBSDFData data, bool thinSurface)
    {
        this.data = data;
        transmissionAlbedo = thinSurface ? data.transmission : sqrt(data.transmission);
        alpha = data.roughness * data.roughness;
        alpha = alpha < kMinGGXAlpha ? 0.0 : alpha;
        transmissionAlpha = data.eta == 1.0 ? 0.0 : alpha;

        float opaque = 1.0 - data.specularTransmission;
        float diffuseWeight = rec709Luminance(data.diffuse) * (1.0 - data.metallic) * opaque;
        float specularWeight = rec709Luminance(evalFresnelSchlick(data.specular, 1.0, wi.z)) * opaque;
        probabilities = float4(diffuseWeight * (1.0 - data.diffuseTransmission),
            diffuseWeight * data.diffuseTransmission, specularWeight, data.specularTransmission);
        float total = dot(probabilities, float4(1.0));
        if (total > 0.0)
            probabilities /= total;
    }

    float3 evalDiffuseReflection(float3 wi, float3 wo)
    {
        if (min(wi.z, wo.z) < kMinCosTheta)
            return float3(0.0);

        // FrostbiteDiffuseBRDF is Falcor's default diffuse model.
        float3 halfVector = normalize(wi + wo);
        float woDotH = dot(wo, halfVector);
        float fd90 = 0.5 * data.roughness + 2.0 * woDotH * woDotH * data.roughness;
        float wiScatter = 1.0 + (fd90 - 1.0) * pow(max(1.0 - wi.z, 0.0), 5.0);
        float woScatter = 1.0 + (fd90 - 1.0) * pow(max(1.0 - wo.z, 0.0), 5.0);
        float energyFactor = lerp(1.0, 1.0 / 1.51, data.roughness);

        return data.diffuse * wiScatter * woScatter * energyFactor * INV_PI * wo.z;
    }

    float3 evalSpecularReflection(float3 wi, float3 wo)
    {
        if (alpha == 0.0 || min(wi.z, wo.z) < kMinCosTheta)
            return float3(0.0);

        float3 halfVector = normalize(wi + wo);
        float distribution = evalNdfGGX(alpha, halfVector.z);
        float masking = evalMaskingSmithGGXCorrelated(alpha, wi.z, wo.z);

        return evalFresnelSchlick(data.specular, 1.0, dot(wi, halfVector)) * distribution * masking * 0.25 / wi.z;
    }

    float3 evalSpecularTransmission(float3 wi, float3 wo)
    {
        if (transmissionAlpha == 0.0 || min(wi.z, abs(wo.z)) < kMinCosTheta)
            return float3(0.0);

        bool reflection = wo.z > 0.0;
        float3 halfVector = normalize(wo + wi * (reflection ? 1.0 : data.eta));
        halfVector *= sign(halfVector.z);
        float wiDotH = dot(wi, halfVector);
        float woDotH = dot(wo, halfVector);
        float cosThetaT;
        float fresnel = evalFresnelDielectric(data.eta, wiDotH, cosThetaT);
        float distribution = evalNdfGGX(transmissionAlpha, halfVector.z);
        float masking = evalMaskingSmithGGXCorrelated(transmissionAlpha, wi.z, abs(wo.z));
        if (reflection)
            return float3(fresnel * distribution * masking * 0.25 / wi.z);

        float denominator = woDotH + data.eta * wiDotH;
        float factor = data.eta * data.eta * wiDotH * woDotH / (wi.z * denominator * denominator);

        return transmissionAlbedo * (1.0 - fresnel) * distribution * masking * abs(factor);
    }

    float3 eval(float3 wi, float3 wo)
    {
        if (min(wi.z, abs(wo.z)) < kMinCosTheta)
            return float3(0.0);

        float opaque = 1.0 - data.specularTransmission;
        float3 result = float3(0.0);
        if (pDiffuseReflection > 0.0)
            result += opaque * (1.0 - data.diffuseTransmission) * evalDiffuseReflection(wi, wo);
        if (pDiffuseTransmission > 0.0 && wo.z < 0.0)
            result += opaque * data.diffuseTransmission * transmissionAlbedo * INV_PI * -wo.z;
        if (pSpecularReflection > 0.0)
            result += opaque * evalSpecularReflection(wi, wo);
        if (pSpecularTransmission > 0.0)
            result += data.specularTransmission * evalSpecularTransmission(wi, wo);

        return result;
    }

    float reflectionPdf(float3 wi, float3 wo)
    {
        if (alpha == 0.0 || min(wi.z, wo.z) < kMinCosTheta)
            return 0.0;

        float3 halfVector = normalize(wi + wo);

        return evalPdfGGX_VNDF(alpha, wi, halfVector) / (4.0 * dot(wi, halfVector));
    }

    float transmissionPdf(float3 wi, float3 wo)
    {
        if (transmissionAlpha == 0.0 || min(wi.z, abs(wo.z)) < kMinCosTheta)
            return 0.0;

        bool reflection = wo.z > 0.0;
        float3 halfVector = normalize(wo + wi * (reflection ? 1.0 : data.eta));
        halfVector *= sign(halfVector.z);
        float wiDotH = dot(wi, halfVector);
        float woDotH = dot(wo, halfVector);
        float cosThetaT;
        float fresnel = evalFresnelDielectric(data.eta, wiDotH, cosThetaT);
        float pdf = evalPdfGGX_VNDF(transmissionAlpha, wi, halfVector);
        if (reflection)
            return pdf * fresnel / (4.0 * woDotH);
        if (woDotH > 0.0)
            return 0.0;

        float denominator = woDotH + data.eta * wiDotH;
        float denominatorSquared = denominator * denominator;

        return (denominatorSquared > 0.0 ? pdf * abs(woDotH) / denominatorSquared : 3.402823466e38)
            * (1.0 - fresnel);
    }

    float evalPdf(float3 wi, float3 wo)
    {
        if (min(wi.z, abs(wo.z)) < kMinCosTheta)
            return 0.0;

        float pdf = (wo.z > 0.0 ? pDiffuseReflection : pDiffuseTransmission) * abs(wo.z) * INV_PI;
        if (pSpecularReflection > 0.0)
            pdf += pSpecularReflection * reflectionPdf(wi, wo);
        if (pSpecularTransmission > 0.0)
            pdf += pSpecularTransmission * transmissionPdf(wi, wo);

        return pdf;
    }

    BSDFSample sample<S : ISampleGenerator>(float3 wi, inout S generator)
    {
        BSDFSample result = {};
        if (wi.z < kMinCosTheta)
            return result;

        float select = sampleNext1D(generator);
        float opaque = 1.0 - data.specularTransmission;
        if (select < pDiffuseReflection + pDiffuseTransmission)
        {
            float2 u = sampleNext2D(generator) * 2.0 - 1.0;
            float radius = 0.0;
            float phi = 0.0;
            if (any(u != 0.0))
            {
                bool horizontal = abs(u.x) > abs(u.y);
                radius = horizontal ? u.x : u.y;
                phi = horizontal ? (PI * 0.25 * u.y / u.x) : (PI * 0.5 - PI * 0.25 * u.x / u.y);
            }
            float3 wo = float3(radius * cos(phi), radius * sin(phi), sqrt(max(0.0, 1.0 - radius * radius)));
            bool reflection = select < pDiffuseReflection;
            wo.z *= reflection ? 1.0 : -1.0;
            if (abs(wo.z) < kMinCosTheta)
                return result;

            result.direction = wo;
            result.weight = reflection
                ? evalDiffuseReflection(wi, wo) / (wo.z * INV_PI) * opaque * (1.0 - data.diffuseTransmission) / pDiffuseReflection
                : transmissionAlbedo * opaque * data.diffuseTransmission / pDiffuseTransmission;
        }
        else if (select < pDiffuseReflection + pDiffuseTransmission + pSpecularReflection)
        {
            result.isDelta = alpha == 0.0;
            float3 halfVector = result.isDelta ? float3(0.0, 0.0, 1.0)
                : sampleGGX_VNDF(alpha, wi, sampleNext2D(generator));
            float wiDotH = dot(wi, halfVector);
            result.direction = 2.0 * wiDotH * halfVector - wi;
            if (result.direction.z < kMinCosTheta)
                return result;

            float maskingWeight = result.isDelta ? 1.0
                : evalMaskingSmithGGXCorrelated(alpha, wi.z, result.direction.z)
                    * (1.0 + evalLambdaGGX(alpha * alpha, wi.z));
            result.weight = evalFresnelSchlick(data.specular, 1.0, wiDotH) * maskingWeight * opaque / pSpecularReflection;
        }
        else if (pSpecularTransmission > 0.0)
        {
            float lobeSample = sampleNext1D(generator);
            result.isDelta = transmissionAlpha == 0.0;
            float3 halfVector = result.isDelta ? float3(0.0, 0.0, 1.0)
                : sampleGGX_VNDF(transmissionAlpha, wi, sampleNext2D(generator));
            float wiDotH = dot(wi, halfVector);
            float cosThetaT;
            float fresnel = evalFresnelDielectric(data.eta, wiDotH, cosThetaT);
            bool reflection = lobeSample < fresnel;
            result.direction = reflection ? 2.0 * wiDotH * halfVector - wi
                : (data.eta * wiDotH - cosThetaT) * halfVector - data.eta * wi;
            if (abs(result.direction.z) < kMinCosTheta || ((result.direction.z > 0.0) != reflection))
                return result;

            float maskingWeight = result.isDelta ? 1.0
                : evalMaskingSmithGGXCorrelated(transmissionAlpha, wi.z, abs(result.direction.z))
                    * (1.0 + evalLambdaGGX(transmissionAlpha * transmissionAlpha, wi.z));
            result.weight = maskingWeight * data.specularTransmission / pSpecularTransmission;
            if (!reflection)
                result.weight *= transmissionAlbedo * data.eta * data.eta;
        }
        else
            return result;

        result.valid = true;
        result.pdf = result.isDelta ? 0.0 : evalPdf(wi, result.direction);

        return result;
    }
};

#endif
