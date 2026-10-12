#ifndef RENDERLAB_SCENE_LIGHTING_SLANG
#define RENDERLAB_SCENE_LIGHTING_SLANG

#include "brdf.slang"
#include "scene_data.slang"
#include "sampling.slang"
#include "surface.slang"

#ifndef ENABLE_IBL
#error ENABLE_IBL must be specified when compiling scene lighting
#endif

// Scene shadow resources (bindings 6..10).
[[vk::binding(6, 0)]]
TextureCubeArray<float> pointShadowMap;

[[vk::binding(7, 0)]]
Texture2DArray<float> rectAreaShadowMap;

[[vk::binding(8, 0)]]
Texture2DArray<float> cascadeShadowMap;

[[vk::binding(9, 0)]]
SamplerComparisonState shadowSampler;

[[vk::binding(10, 0)]]
SamplerState linearSampler;

// Scene environment image / sampler pairs (bindings 11..18).
[[vk::binding(11, 0)]]
TextureCube<float4> irradianceMap;

[[vk::binding(12, 0)]]
SamplerState irradianceMapSampler;

[[vk::binding(13, 0)]]
TextureCube<float4> prefilteredSpecularMap;

[[vk::binding(14, 0)]]
SamplerState prefilteredSpecularMapSampler;

[[vk::binding(15, 0)]]
Texture2D<float4> brdfLUT;

[[vk::binding(16, 0)]]
SamplerState brdfLUTSampler;

float directionalShadowVisibility(LightData light, float3 worldPosition)
{
    float viewDepth = -mul(viewUniforms.view, float4(worldPosition, 1.0)).z;
    if (viewDepth <= 0.0)
        return 1.0;

    [loop]
    for (uint cascadeIndex = 0; cascadeIndex < lightUniforms.cascadeCount; ++cascadeIndex)
    {
        CascadeShadowData cascade = directionalShadowData[light.shadowIdx].cascades[cascadeIndex];
        if (viewDepth > cascade.depthUpperBound)
            continue;

        float4 clip = mul(cascade.vp, float4(worldPosition, 1.0));
        float3 projected = clip.xyz / clip.w;
        float2 uv = projected.xy * 0.5 + 0.5;
        if (any(uv < 0.0) || any(uv > 1.0) || projected.z < 0.0 || projected.z > 1.0)
            return 1.0;

        uint layer = light.shadowIdx * lightUniforms.cascadeCount + cascadeIndex;
        return cascadeShadowMap.SampleCmpLevelZero(
            shadowSampler, float3(uv, layer), projected.z - lightUniforms.shadowBias);
    }
    // Beyond the configured shadow distance, only direct lighting remains.
    return 1.0;
}


float pcf(float3 dir, uint shadowIndex, float depth, float2 rotation)
{
    float3 direction = normalize(dir);
    float3 referenceAxis = abs(direction.z) < 0.999
        ? float3(0.0, 0.0, 1.0)
        : float3(0.0, 1.0, 0.0);
    float3 tangent = normalize(cross(direction, referenceAxis));
    float3 bitangent = cross(direction, tangent);
    float3 rotatedTangent = tangent * rotation.x + bitangent * rotation.y;
    float3 rotatedBitangent = bitangent * rotation.x - tangent * rotation.y;

    float totalVisibility = 0.0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kPoissonDisk16SampleCount; ++sampleIndex)
    {
        float2 offset = kPoissonDisk16[sampleIndex] * lightUniforms.shadowSampleRadius;
        float3 sampleDirection = normalize(
            direction + rotatedTangent * offset.x + rotatedBitangent * offset.y);
        totalVisibility += pointShadowMap.SampleCmpLevelZero(
            shadowSampler, float4(sampleDirection, shadowIndex), depth - lightUniforms.shadowBias);
    }

    return totalVisibility / kPoissonDisk16SampleCount;
}


float rectAreaShadowPCSS(LightData light, float3 worldPos, uint2 pixel)
{
    uint shadowIndex = light.shadowIdx;
    RectAreaShadowData shadow = rectAreaShadowData[shadowIndex];
    float4 pos = mul(shadow.vp, float4(worldPos, 1.0));
    if (pos.w <= 0.0)
        return 1.0;

    float3 projected = pos.xyz / pos.w;
    float2 uv = projected.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || projected.z < 0.0 || projected.z > 1.0)
        return 1.0;

    // clip.w is the positive, linear depth along the light's viewing direction.
    float receiverLinearDepth = pos.w;
    float receiverDepth = max(projected.z - lightUniforms.shadowBias, 0.0);
    float2 lightHalfSize = light.areaSize * 0.5;

    // The blocker depth is unknown: project the search footprint onto the near plane.
    float2 searchSize = lightHalfSize * max(receiverLinearDepth - shadow.nearPlane, 0.0)
        / receiverLinearDepth;
    float2 searchRadiusUv = searchSize * shadow.worldToUvScale / shadow.nearPlane;
    uint blockerCount = 0;
    float avgBlockerLinearDepth = 0.0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kRectangle16SampleCount; ++sampleIndex)
    {
        float2 offset = rectangleSample16(sampleIndex, pixel, lightUniforms.shadowPixelRotation);
        float2 sampleUv = uv + offset * searchRadiusUv;
        if (any(sampleUv < 0.0) || any(sampleUv > 1.0))
            continue;

        float blockerDepth = rectAreaShadowMap.SampleLevel(
            linearSampler, float3(sampleUv, shadowIndex), 0.0);
        // Keep bias and comparison in the same non-linear 0..1 depth space as PCF.
        if (blockerDepth < receiverDepth)
        {
            float blockerLinearDepth = shadow.nearPlane * shadow.farPlane
                / (shadow.farPlane + (shadow.nearPlane - shadow.farPlane) * blockerDepth);
            avgBlockerLinearDepth += blockerLinearDepth;
            ++blockerCount;
        }
    }

    if (blockerCount == 0)
        return 1.0;
    avgBlockerLinearDepth /= blockerCount;

    // Small-triangle footprint on the blocker plane; convert using blocker depth.
    float ratio = max(receiverLinearDepth - avgBlockerLinearDepth, 0.0) / receiverLinearDepth;
    float2 filterSize = lightHalfSize * ratio;
    float2 filterRadiusUv = filterSize * shadow.worldToUvScale / avgBlockerLinearDepth;
    float totalVisibility = 0.0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < kRectangle16SampleCount; ++sampleIndex)
    {
        float2 offset = rectangleSample16(sampleIndex, pixel, lightUniforms.shadowPixelRotation);
        float2 sampleUv = uv + offset * filterRadiusUv;
        if (any(sampleUv < 0.0) || any(sampleUv > 1.0))
        {
            totalVisibility += 1.0;
            continue;
        }

        totalVisibility += rectAreaShadowMap.SampleCmpLevelZero(
            shadowSampler, float3(sampleUv, shadowIndex), receiverDepth);
    }
    return totalVisibility / kRectangle16SampleCount;
}

float3 evaluateLighting(Surface surface, uint2 pixel)
{
    float3 V = normalize(viewUniforms.cameraPosition.xyz - surface.worldPosition);
    float NdotV = saturate(dot(surface.normal, V));
    float3 f0 = lerp(0.04, surface.baseColor.rgb, float3(surface.metallic));
#if ENABLE_CLEARCOAT
    float3 clearcoatNormal = surface.clearcoatNormal;
    float clearcoatRoughness = surface.clearcoatRoughness;
    float clearcoatNdotV = saturate(dot(clearcoatNormal, V));
    float clearcoatWeight = surface.clearcoatFactor * fresnelSchlick(float3(kClearcoatF0), clearcoatNdotV).x;
#endif

    float2 shadowRotation = lightUniforms.shadowPixelRotation
        ? pixelSampleRotation(pixel)
        : float2(1.0, 0.0);
    float3 direct = float3(0.0);
    for (uint index = 0; index < lightUniforms.lightCount; ++index)
    {
        LightData light = lights[index];
        if (!light.enabled) continue;

        float3 lightDirection;
        // Incident illuminance on a plane perpendicular to lightDirection (lux), before NdotL.
        float3 incidentIlluminance;
        float visibility = 1.0;

        if (light.type == LightType.Point)
        {
            float distanceToLight = length(light.position - surface.worldPosition);
            if (distanceToLight <= 0.0 || distanceToLight > light.range)
                continue;

            lightDirection = (light.position - surface.worldPosition) / distanceToLight;
            incidentIlluminance = light.color * light.luminousIntensity / (distanceToLight * distanceToLight);
            if (light.castShadow)
            {
                float depth = distanceToLight / light.range;
                visibility = pcf(-lightDirection, light.shadowIdx, depth, shadowRotation);
            }
        }
        else if (light.type == LightType.Directional)
        {
            lightDirection = -light.direction.xyz;
            incidentIlluminance = light.color * light.illuminance;
            if (light.castShadow)
                visibility = directionalShadowVisibility(light, surface.worldPosition);
        }
        else if (light.type == LightType.RectArea)
        {
            float distanceToLight = length(light.position - surface.worldPosition);
            if (distanceToLight <= 0.0)
                continue;

            // Center-direction approximation; finite-area specular integration is not included.
            lightDirection = (light.position - surface.worldPosition) / distanceToLight;
            float projectedArea = light.areaSize.x * light.areaSize.y
                * saturate(dot(-lightDirection, light.direction.xyz));
            float solidAngle = projectedArea / (distanceToLight * distanceToLight);
            incidentIlluminance = light.color * light.luminance * solidAngle;
            if (light.castShadow)
                visibility = rectAreaShadowPCSS(light, surface.worldPosition, pixel);
        }
        else
            continue;

        float nDotL = saturate(dot(surface.normal, lightDirection));
        if (visibility <= 0.0)
            continue;

        float3 brdf = evaluateDirectBrdf(surface.baseColor.rgb, surface.metallic, surface.roughness, surface.normal, V, lightDirection);
        float3 response = brdf * nDotL;
#if ENABLE_CLEARCOAT
        float clearcoatNdotL = saturate(dot(clearcoatNormal, lightDirection));
        float clearcoatResponse = evaluateClearcoatBrdf(clearcoatRoughness, clearcoatNormal, V, lightDirection)
            * clearcoatNdotL;
        response = lerp(response, float3(clearcoatResponse), clearcoatWeight);
#endif
        direct += incidentIlluminance * response * visibility;
    }

    float3 ambient = float3(0.0);
#if ENABLE_IBL
    // 环境光照
    float3 R = reflect(-V, surface.normal);

    // 漫反射环境光照
    float3 irradiance = irradianceMap.Sample(irradianceMapSampler, surface.normal).rgb;
    float3 ambientDiffuse = irradiance * surface.baseColor.rgb * INV_PI * (1.0 - surface.metallic);

    // 镜面反射环境光照 Split Sum Approximation
    float3 environmentTerm = prefilteredSpecularMap.SampleLevel(
        prefilteredSpecularMapSampler, R, surface.roughness * lightUniforms.iblPrefilteredMaxMipLevel).rgb;
    float2 scaleBias = brdfLUT.Sample(brdfLUTSampler, float2(NdotV, surface.roughness)).rg;
    float3 brdfTerm = scaleBias.x * f0 + scaleBias.y;
    float3 ambientSpecular = environmentTerm * brdfTerm;

    ambient = ambientDiffuse + ambientSpecular;
#else
    ambient = lightUniforms.ambientCoefficient * surface.baseColor.rgb;
#endif
#if ENABLE_CLEARCOAT
    ambient *= 1.0 - clearcoatWeight;
#if ENABLE_IBL
    float3 clearcoatReflection = reflect(-V, clearcoatNormal);
    float3 clearcoatEnvironment = prefilteredSpecularMap.SampleLevel(
        prefilteredSpecularMapSampler,
        clearcoatReflection,
        clearcoatRoughness * lightUniforms.iblPrefilteredMaxMipLevel).rgb;
    float2 clearcoatScaleBias = brdfLUT.Sample(brdfLUTSampler, float2(clearcoatNdotV, clearcoatRoughness)).rg;
    // The layer weight already includes Fresnel. Use the LUT's unit-Fresnel
    // response (scale + bias), avoiding a second Fresnel multiplication.
    ambient += clearcoatEnvironment * (clearcoatScaleBias.x + clearcoatScaleBias.y) * clearcoatWeight;
#endif
#endif
    ambient *= surface.occlusion;

    float3 emissive = surface.emissive;
#if ENABLE_CLEARCOAT
    emissive *= 1.0 - clearcoatWeight;
#endif
    float3 color = direct + ambient + emissive;

    return color;
}

#endif
