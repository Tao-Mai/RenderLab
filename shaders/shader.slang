#include "brdf.slang"
#include "scene_data.slang"
#include "sampling.slang"
#include "mesh_vertex.slang"
#include "material_data.slang"

#ifndef ENABLE_IBL
#error ENABLE_IBL must be specified when compiling the scene shader
#endif

struct VertexOutput
{
    precise float4 position : SV_Position;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldPosition : POSITION0;
    float4 worldTangent : TEXCOORD1;
};

[[vk::binding(3, 1)]]
Texture2D<float4> normalTexture;

[[vk::binding(4, 1)]]
SamplerState normalSampler;

[[vk::binding(5, 1)]]
Texture2D<float4> metallicTexture;

[[vk::binding(6, 1)]]
SamplerState metallicSampler;

[[vk::binding(7, 1)]]
Texture2D<float4> roughnessTexture;

[[vk::binding(8, 1)]]
SamplerState roughnessSampler;

[[vk::binding(9, 1)]]
Texture2D<float4> emissiveTexture;

[[vk::binding(10, 1)]]
SamplerState emissiveSampler;

[[vk::binding(11, 1)]]
Texture2D<float4> occlusionTexture;

[[vk::binding(12, 1)]]
SamplerState occlusionSampler;

// Scene shadow resources (bindings 5..8).
[[vk::binding(5, 0)]]
TextureCubeArray<float> pointShadowMap;

[[vk::binding(6, 0)]]
Texture2DArray<float> rectAreaShadowMap;

[[vk::binding(7, 0)]]
SamplerComparisonState shadowSampler;

[[vk::binding(8, 0)]]
SamplerState linearSampler;

// Scene environment image / sampler pairs (bindings 9..16).
[[vk::binding(9, 0)]]
TextureCube<float4> irradianceMap;

[[vk::binding(10, 0)]]
SamplerState irradianceMapSampler;

[[vk::binding(11, 0)]]
TextureCube<float4> prefilteredSpecularMap;

[[vk::binding(12, 0)]]
SamplerState prefilteredSpecularMapSampler;

[[vk::binding(13, 0)]]
Texture2D<float4> brdfLUT;

[[vk::binding(14, 0)]]
SamplerState brdfLUTSampler;



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

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition;
    output.position = transformMeshPosition(input.position, worldPosition);
    float3x3 model = (float3x3)pushConstants.model;
    // cofactor矩阵，伴随矩阵的转置
    float3x3 cofactor = float3x3(
        cross(model[1], model[2]),
        cross(model[2], model[0]),
        cross(model[0], model[1]));
    float handedness = determinant(model) < 0.0 ? -1.0 : 1.0;
    output.worldNormal = normalize(mul(cofactor, input.normal)) * handedness;
    output.worldTangent = float4(normalize(mul(model, input.tangent.xyz)), input.tangent.w * handedness);
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    float3 N = normalize(input.worldNormal);
    float3 T = normalize(input.worldTangent.xyz - N * dot(N, input.worldTangent.xyz));
    float3 B = cross(N, T) * input.worldTangent.w;
    float3 normalTs = normalTexture.Sample(normalSampler, input.uv).xyz * 2.0 - 1.0;
    normalTs.xy *= material.normalScale;
    N = normalize(T * normalTs.x + B * normalTs.y + N * normalTs.z);

    float4 baseColor = baseColorTexture.Sample(baseColorSampler, input.uv) * material.baseColorFactor;
    applyAlphaTest(baseColor.a);

    float3 V = normalize(viewUniforms.cameraPosition.xyz - input.worldPosition);
    float NdotV = saturate(dot(N, V));
    // glTF metallic-roughness textures store roughness in G and metallic in B.
    float roughness = saturate(material.roughness * roughnessTexture.Sample(roughnessSampler, input.uv).g);
    float metallic = saturate(material.metallic * metallicTexture.Sample(metallicSampler, input.uv).b);
    float3 F0 = lerp(0.04, baseColor.rgb, float3(metallic));

    float2 shadowRotation = lightUniforms.shadowPixelRotation
        ? pixelSampleRotation(uint2(input.position.xy))
        : float2(1.0, 0.0);
    float3 direct = float3(0.0);
    for (uint index = 0; index < lightUniforms.lightCount; ++index)
    {
        LightData light = lights[index];
        if (!light.enabled) continue;

        if (light.type == LightType.Point) {
            float3 L = normalize(light.position - input.worldPosition);
            float distanceToLight = length(light.position - input.worldPosition);
            if (distanceToLight > light.range) continue;

            float visibility = 1.0;
            if (light.castShadow) {
                uint shadowidx = light.shadowIdx;
                float depth = distanceToLight / light.range;

                // depth <= shadowMapDepth，用的less or equal
                visibility = pcf(-L, shadowidx, depth, shadowRotation);

                if (visibility == 0.0) continue;
            }

            float3 pointLight = light.color * light.intensity / (distanceToLight * distanceToLight);

            float3 H = normalize(L + V);
            float NdotH = saturate(dot(N, H));
            float NdotL = saturate(dot(N, L));
            float VdotH = saturate(dot(V, H));
            float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
            float D = NormalDistributionGGX(roughness, NdotH);
            float G = GeometrySmithGGX(roughness, NdotV, NdotL);

            float3 diffuseBRDF = (1.0 - F) * (1.0 - metallic) * baseColor.rgb * INV_PI;
            float3 specularBRDF = F * D * G / max(4.0 * NdotV * NdotL, 1e-5);
            direct += pointLight * (diffuseBRDF + specularBRDF) * NdotL * visibility;
        }
        else if (light.type == LightType.Directional) {
            // TODO
            float NdotL = saturate(dot(N, -light.direction.xyz));
            direct += light.color * light.intensity * NdotL * baseColor.rgb;
        } else if (light.type == LightType.RectArea) {
            float3 L = normalize(light.position - input.worldPosition);
            float distanceToLight = length(light.position - input.worldPosition);
            float NdotL = saturate(dot(N, L));
            float projectedArea = light.areaSize.x * light.areaSize.y * saturate(dot(-L, light.direction.xyz));
            float omegaRange = projectedArea / (distanceToLight * distanceToLight);

            float visibility = 1.0;
            if (light.castShadow) {
                visibility = rectAreaShadowPCSS(light, input.worldPosition, uint2(input.position.xy));
            }

            direct += light.color * light.radiance * NdotL * omegaRange * INV_PI * visibility * baseColor.rgb;
        }
    }

    float3 ambient = float3(0.0);
#if ENABLE_IBL
    // 环境光照
    float3 R = reflect(-V, N);

    // 漫反射环境光照
    float3 irradiance = irradianceMap.Sample(irradianceMapSampler, N).rgb;
    float3 ambient_diffuse = irradiance * baseColor.rgb * INV_PI * (1.0 - metallic);

    // 镜面反射环境光照 Split Sum Approximation
    float3 env_term = prefilteredSpecularMap.SampleLevel(
        prefilteredSpecularMapSampler, R, roughness * lightUniforms.iblPrefilteredMaxMipLevel).rgb;
    float2 scale_bias = brdfLUT.Sample(brdfLUTSampler, float2(NdotV, roughness)).rg;
    float3 brdf_term = scale_bias.x * F0 + scale_bias.y;
    float3 ambient_specular = env_term * brdf_term;

    ambient = ambient_diffuse + ambient_specular;
#else
    ambient = lightUniforms.ambientCoefficient * baseColor.rgb;
#endif
    float occlusion = lerp(1.0, occlusionTexture.Sample(occlusionSampler, input.uv).r, material.occlusionStrength);
    ambient *= occlusion;

    float3 emissive = emissiveTexture.Sample(emissiveSampler, input.uv).rgb * material.emissiveFactor.rgb;
    float3 color = direct + ambient + emissive;

    return float4(color, baseColor.a);
}
