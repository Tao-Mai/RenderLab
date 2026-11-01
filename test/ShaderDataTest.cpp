#include <vulkan/vulkan.hpp>

#include "core/Logger.h"
#include "render/editor/EditorShaderData.h"
#include "render/raytracing/realtime/RayTracingData.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/resources/MaterialData.h"
#include "render/resources/ShaderData.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <utility>
#include <tuple>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
struct Field
{
    std::string_view name;
    size_t           offset;
    size_t           size;
};

json parameter(std::string_view shader, std::string_view name)
{
    const auto path = std::filesystem::path(RENDERLAB_SHADER_BINARY_DIR) / (std::string(shader) + ".reflection.json");
    std::ifstream input{path};
    CHECK(input.is_open(), "cannot open shader reflection: {}", path.string());
    const auto reflection = json::parse(input, nullptr, false);
    CHECK(!reflection.is_discarded(), "invalid shader reflection: {}", path.string());
    for (const auto& value : reflection.at("parameters").get_ref<const json::array_t&>())
        if (value.at("name").get<std::string>() == name)
            return value;
    CHECK(false, "missing shader parameter: {}", name);
}

void checkFields(const json& type, std::span<const Field> expected)
{
    const auto& fields = type.at("fields").get_ref<const json::array_t&>();
    ASSERT_EQ(fields.size(), expected.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
        SCOPED_TRACE(expected[index].name);
        const auto& value = fields.at(index);
        EXPECT_EQ(value.at("name").get<std::string>(), expected[index].name);
        const auto& binding = value.at("binding");
        EXPECT_EQ(binding.at("offset").get<size_t>(), expected[index].offset);
        EXPECT_EQ(binding.at("size").get<size_t>(), expected[index].size);
    }
}

constexpr std::array sceneShaders = {
    "slang", "slang.no_ibl", "slang.clearcoat", "slang.clearcoat.no_ibl", "depth", "skybox",
    "raytracing", "raytracing.svgf", "offline_pathtracing", "svgf_reprojection"
};

constexpr std::array lightingShaders = {
    "slang",
    "slang.no_ibl",
    "slang.clearcoat",
    "slang.clearcoat.no_ibl",
    "shadow",
    "shadow.rect_area",
    "shadow.directional",
    "shadow.spot"
};
}  // namespace

TEST(ShaderDataLayout, SceneUniformsMatchCompiledShaders)
{
    const std::array expected = {
        Field{"viewProjection", offsetof(SceneUniforms, viewProjection), sizeof(glm::mat4)},
        Field{"inverseViewProjection", offsetof(SceneUniforms, inverseViewProjection), sizeof(glm::mat4)},
        Field{"cameraPosition", offsetof(SceneUniforms, cameraPosition), sizeof(glm::vec4)},
        Field{"view", offsetof(SceneUniforms, view), sizeof(glm::mat4)},
    };
    for (const auto* shader : sceneShaders)
    {
        SCOPED_TRACE(shader);
        const auto value = parameter(shader, "sceneUniforms");
        EXPECT_EQ(value.at("binding").value("space", 0u), RenderInterface::kSceneSet);
        EXPECT_EQ(value.at("binding").at("index").get<uint32_t>(), RenderInterface::kSceneUniformBinding);
        const auto& type = value.at("type");
        EXPECT_EQ(type.at("kind"), "constantBuffer");
        checkFields(type.at("elementType"), expected);
        EXPECT_EQ(type.at("elementVarLayout").at("binding").at("size").get<size_t>(), sizeof(SceneUniforms));
    }
}

TEST(ShaderDataLayout, LightUniformsMatchCompiledShaders)
{
    const std::array expected = {
        Field{"lightRanges", offsetof(LightUniforms, lightRanges), sizeof(LightRanges)},
        Field{"ambientCoefficient", offsetof(LightUniforms, ambientCoefficient), sizeof(float)},
        Field{"prefilteredMaxLod", offsetof(LightUniforms, prefilteredMaxLod), sizeof(float)},
        Field{"padding", offsetof(LightUniforms, padding), sizeof(glm::vec2)},
    };
    for (const auto* shader : lightingShaders)
    {
        SCOPED_TRACE(shader);
        const auto value = parameter(shader, "lightUniforms");
        EXPECT_EQ(value.at("binding").at("space"), RenderInterface::kLightSet);
        EXPECT_EQ(value.at("binding").at("index").get<uint32_t>(), RenderInterface::kLightUniformBinding);
        const auto& type = value.at("type");
        EXPECT_EQ(type.at("kind"), "constantBuffer");
        checkFields(type.at("elementType"), expected);
        EXPECT_EQ(type.at("elementVarLayout").at("binding").at("size").get<size_t>(), sizeof(LightUniforms));
    }
}

TEST(ShaderDataLayout, ShadowUniformsMatchCompiledShaders)
{
    const std::array expected = {
        Field{"cascadeCount", offsetof(ShadowUniforms, cascadeCount), sizeof(uint32_t)},
        Field{"directionalSampleRadius", offsetof(ShadowUniforms, directionalSampleRadius), sizeof(float)},
        Field{"bias", offsetof(ShadowUniforms, bias), sizeof(float)},
        Field{"sampleRadius", offsetof(ShadowUniforms, sampleRadius), sizeof(float)},
        Field{"spotSampleRadius", offsetof(ShadowUniforms, spotSampleRadius), sizeof(float)},
        Field{"pixelRotation", offsetof(ShadowUniforms, pixelRotation), sizeof(uint32_t)},
        Field{"enabled", offsetof(ShadowUniforms, enabled), sizeof(uint32_t)},
        Field{"padding", offsetof(ShadowUniforms, padding), sizeof(uint32_t)},
    };
    for (const auto* shader : lightingShaders)
    {
        SCOPED_TRACE(shader);
        const auto value = parameter(shader, "shadowUniforms");
        EXPECT_EQ(value.at("binding").at("space"), RenderInterface::kShadowSet);
        EXPECT_EQ(value.at("binding").at("index"), RenderInterface::kShadowUniformBinding);
        checkFields(value.at("type").at("elementType"), expected);
        EXPECT_EQ(value.at("type").at("elementVarLayout").at("binding").at("size"), sizeof(ShadowUniforms));
    }
}

TEST(ShaderDataLayout, LightStorageElementsMatchCompiledShaders)
{
    const std::array expected = {
        Field{"color", offsetof(LightData, color), sizeof(glm::vec3)},
        Field{"padding3", offsetof(LightData, padding3), sizeof(float)},
        Field{"positionRange", offsetof(LightData, positionRange), sizeof(glm::vec4)},
        Field{"direction", offsetof(LightData, direction), sizeof(glm::vec4)},
        Field{"areaSizeCone", offsetof(LightData, areaSizeCone), sizeof(glm::vec4)},
        Field{"lightType", offsetof(LightData, lightType), sizeof(GpuLightType)},
        Field{"shadowEnabled", offsetof(LightData, shadowEnabled), sizeof(uint32_t)},
        Field{"shadowIdx", offsetof(LightData, shadowIdx), sizeof(uint32_t)},
        Field{"padding0", offsetof(LightData, padding0), sizeof(uint32_t)},
        Field{"areaRightData", offsetof(LightData, areaRightData), sizeof(glm::vec4)},
    };
    for (const auto* shader :
         {"slang",
          "slang.no_ibl",
          "slang.clearcoat",
          "slang.clearcoat.no_ibl",
          "shadow",
          "shadow.rect_area",
          "shadow.directional",
          "shadow.spot",
          "raytracing"})
    {
        SCOPED_TRACE(shader);
        const auto value = parameter(shader, "lights");
        EXPECT_EQ(value.at("binding").at("space"), RenderInterface::kLightSet);
        EXPECT_EQ(value.at("binding").at("index").get<uint32_t>(), RenderInterface::kLightBufferBinding);
        const auto& type = value.at("type");
        EXPECT_EQ(type.at("baseShape"), "structuredBuffer");
        checkFields(type.at("resultType"), expected);
        EXPECT_EQ(expected.back().offset + expected.back().size, sizeof(LightData));
    }
}

TEST(ShaderDataLayout, PointShadowStorageAndPushConstantsMatchCompiledShader)
{
    const auto shadow = parameter("shadow", "pointShadowData");
    EXPECT_EQ(shadow.at("binding").at("space"), RenderInterface::kShadowSet);
    EXPECT_EQ(shadow.at("binding").at("index").get<uint32_t>(), RenderInterface::kPointShadowBufferBinding);
    const std::array expected = {Field{"vp", offsetof(PointShadowData, vp), sizeof(PointShadowData::vp)}};
    checkFields(shadow.at("type").at("resultType"), expected);

    const std::array pushFields = {
        Field{"model", offsetof(ShadowPushConstants, model), sizeof(glm::mat4)},
        Field{"lightIdx", offsetof(ShadowPushConstants, lightIdx), sizeof(uint32_t)},
        Field{"cascadeIndex", offsetof(ShadowPushConstants, cascadeIndex), sizeof(uint32_t)},
        Field{"padding1", offsetof(ShadowPushConstants, padding1), sizeof(uint32_t)},
        Field{"padding2", offsetof(ShadowPushConstants, padding2), sizeof(uint32_t)},
    };
    for (const auto* shader : {"shadow", "shadow.rect_area", "shadow.directional", "shadow.spot"})
        checkFields(parameter(shader, "pushConstants").at("type").at("elementType"), pushFields);
}

TEST(ShaderDataLayout, DirectionalShadowStorageMatchesCompiledShaders)
{
    const std::array expected = {
        Field{"vp", offsetof(CascadeShadowData, vp), sizeof(glm::mat4)},
        Field{"depthUpperBound", offsetof(CascadeShadowData, depthUpperBound), sizeof(float)},
        Field{"blendStartDepth", offsetof(CascadeShadowData, blendStartDepth), sizeof(float)},
        Field{"padding1", offsetof(CascadeShadowData, padding1), sizeof(float)},
        Field{"padding2", offsetof(CascadeShadowData, padding2), sizeof(float)},
    };
    for (const auto* shader : {"shadow.directional", "slang", "slang.no_ibl"})
    {
        const auto shadow = parameter(shader, "directionalShadowData");
        EXPECT_EQ(shadow.at("binding").at("space"), RenderInterface::kShadowSet);
        EXPECT_EQ(shadow.at("binding").at("index"), RenderInterface::kDirectionalShadowBufferBinding);
        const auto& cascades = shadow.at("type").at("resultType").at("fields").at(0);
        EXPECT_EQ(cascades.at("type").at("elementCount"), kMaxDirectionalShadowCascades);
        EXPECT_EQ(cascades.at("binding").at("size"), sizeof(DirectionalShadowData));
        checkFields(cascades.at("type").at("elementType"), expected);
    }
}

TEST(ShaderDataLayout, SpotShadowStorageMatchesCompiledShaders)
{
    const std::array expected = {Field{"vp", offsetof(SpotShadowData, vp), sizeof(glm::mat4)}};
    for (const auto* shader : {"shadow.spot", "slang", "slang.no_ibl", "deferred_lighting"})
    {
        SCOPED_TRACE(shader);
        const auto shadow = parameter(shader, "spotShadowData");
        EXPECT_EQ(shadow.at("binding").at("space"), RenderInterface::kShadowSet);
        EXPECT_EQ(shadow.at("binding").at("index"), RenderInterface::kSpotShadowBufferBinding);
        checkFields(shadow.at("type").at("resultType"), expected);
    }
}

TEST(ShaderDataLayout, RectAreaShadowStorageMatchesCompiledShaders)
{
    const std::array expected = {
        Field{"vp", offsetof(RectAreaShadowData, vp), sizeof(glm::mat4)},
        Field{"nearPlane", offsetof(RectAreaShadowData, nearPlane), sizeof(float)},
        Field{"farPlane", offsetof(RectAreaShadowData, farPlane), sizeof(float)},
        Field{"worldToUvScale", offsetof(RectAreaShadowData, worldToUvScale), sizeof(glm::vec2)},
    };
    for (const auto* shader : {"shadow.rect_area", "slang", "slang.no_ibl"})
    {
        SCOPED_TRACE(shader);
        const auto shadow = parameter(shader, "rectAreaShadowData");
        EXPECT_EQ(shadow.at("binding").at("space"), RenderInterface::kShadowSet);
        EXPECT_EQ(shadow.at("binding").at("index").get<uint32_t>(), RenderInterface::kRectAreaShadowBufferBinding);
        checkFields(shadow.at("type").at("resultType"), expected);
    }
}

TEST(ShaderDataLayout, LightingTextureAndSamplerBindingsMatchCompiledShaders)
{
    const std::array bindings = {
        std::tuple{"pointShadowMap", RenderInterface::kShadowSet, RenderInterface::kPointShadowImageBinding},
        std::tuple{"spotShadowMap", RenderInterface::kShadowSet, RenderInterface::kSpotShadowImageBinding},
        std::tuple{"rectAreaShadowMap", RenderInterface::kShadowSet, RenderInterface::kRectAreaShadowImageBinding},
        std::tuple{"cascadeShadowMap", RenderInterface::kShadowSet, RenderInterface::kDirectionalShadowImageBinding},
        std::tuple{"shadowSampler", RenderInterface::kShadowSet, RenderInterface::kShadowComparisonSamplerBinding},
        std::tuple{"shadowLinearSampler", RenderInterface::kShadowSet, RenderInterface::kShadowLinearSamplerBinding},
        std::tuple{"irradianceMap", RenderInterface::kLightSet, RenderInterface::kIrradianceImageBinding},
        std::tuple{"irradianceMapSampler", RenderInterface::kLightSet, RenderInterface::kIrradianceSamplerBinding},
        std::tuple{"prefilteredSpecularMap", RenderInterface::kLightSet, RenderInterface::kPrefilteredSpecularImageBinding},
        std::tuple{"prefilteredSpecularMapSampler", RenderInterface::kLightSet, RenderInterface::kPrefilteredSpecularSamplerBinding},
        std::tuple{"brdfLUT", RenderInterface::kLightSet, RenderInterface::kBrdfLutImageBinding},
        std::tuple{"brdfLUTSampler", RenderInterface::kLightSet, RenderInterface::kBrdfLutSamplerBinding},
    };
    for (const auto* shader : {"slang", "slang.no_ibl", "slang.clearcoat", "slang.clearcoat.no_ibl"})
    {
        SCOPED_TRACE(shader);
        for (const auto& [name, set, binding] : bindings)
        {
            SCOPED_TRACE(name);
            EXPECT_EQ(parameter(shader, name).at("binding").at("space"), set);
            EXPECT_EQ(parameter(shader, name).at("binding").at("index").get<uint32_t>(), binding);
        }
    }
    EXPECT_EQ(
        parameter("skybox", "radianceMap").at("binding").at("index").get<uint32_t>(),
        RenderInterface::kRadianceImageBinding
    );
    EXPECT_EQ(parameter("skybox", "radianceMap").at("binding").at("space"), RenderInterface::kLightSet);
    EXPECT_EQ(
        parameter("skybox", "radianceSampler").at("binding").at("index").get<uint32_t>(),
        RenderInterface::kRadianceSamplerBinding
    );
}

TEST(ShaderDataLayout, MaterialDataMatchesCompiledShaders)
{
    const std::array fields = {
        Field{"baseColorFactor", offsetof(MaterialData, baseColorFactor), sizeof(glm::vec4)},
        Field{"roughness", offsetof(MaterialData, roughness), sizeof(float)},
        Field{"metallic", offsetof(MaterialData, metallic), sizeof(float)},
        Field{"alphaCutoff", offsetof(MaterialData, alphaCutoff), sizeof(float)},
        Field{"alphaMode", offsetof(MaterialData, alphaMode), sizeof(uint32_t)},
        Field{"normalScale", offsetof(MaterialData, normalScale), sizeof(float)},
        Field{"occlusionStrength", offsetof(MaterialData, occlusionStrength), sizeof(float)},
        Field{"doubleSided", offsetof(MaterialData, doubleSided), sizeof(uint32_t)},
        Field{"padding", offsetof(MaterialData, padding), sizeof(uint32_t)},
        Field{"emissiveFactor", offsetof(MaterialData, emissiveFactor), sizeof(glm::vec4)},
        Field{"sheenColorFactor", offsetof(MaterialData, sheenColorFactor), sizeof(glm::vec3)},
        Field{"sheenRoughnessFactor", offsetof(MaterialData, sheenRoughnessFactor), sizeof(float)},
        Field{"clearcoatFactor", offsetof(MaterialData, clearcoatFactor), sizeof(float)},
        Field{"clearcoatRoughnessFactor", offsetof(MaterialData, clearcoatRoughnessFactor), sizeof(float)},
        Field{"clearcoatNormalScale", offsetof(MaterialData, clearcoatNormalScale), sizeof(float)},
        Field{"clearcoatPadding", offsetof(MaterialData, clearcoatPadding), sizeof(float)},
        Field{"baseColorTexture", offsetof(MaterialData, baseColorTexture), sizeof(TextureRef)},
        Field{"normalTexture", offsetof(MaterialData, normalTexture), sizeof(TextureRef)},
        Field{"metallicTexture", offsetof(MaterialData, metallicTexture), sizeof(TextureRef)},
        Field{"roughnessTexture", offsetof(MaterialData, roughnessTexture), sizeof(TextureRef)},
        Field{"emissiveTexture", offsetof(MaterialData, emissiveTexture), sizeof(TextureRef)},
        Field{"occlusionTexture", offsetof(MaterialData, occlusionTexture), sizeof(TextureRef)},
        Field{"sheenColorTexture", offsetof(MaterialData, sheenColorTexture), sizeof(TextureRef)},
        Field{"sheenRoughnessTexture", offsetof(MaterialData, sheenRoughnessTexture), sizeof(TextureRef)},
        Field{"clearcoatTexture", offsetof(MaterialData, clearcoatTexture), sizeof(TextureRef)},
        Field{"clearcoatRoughnessTexture", offsetof(MaterialData, clearcoatRoughnessTexture), sizeof(TextureRef)},
        Field{"clearcoatNormalTexture", offsetof(MaterialData, clearcoatNormalTexture), sizeof(TextureRef)},
        Field{"texturePadding", offsetof(MaterialData, texturePadding), sizeof(MaterialData::texturePadding)},
    };
    const std::array indices = {
        Field{"textureIndex", offsetof(TextureRef, textureIndex), sizeof(uint32_t)},
        Field{"samplerIndex", offsetof(TextureRef, samplerIndex), sizeof(uint32_t)},
    };
    for (const auto* shader :
         {"slang",
          "slang.no_ibl",
          "depth",
          "slang.clearcoat",
          "slang.clearcoat.no_ibl",
          "slang.sheen",
          "slang.sheen.no_ibl",
          "slang.clearcoat.sheen",
          "slang.clearcoat.sheen.no_ibl",
          "gbuffer",
          "raytracing"})
    {
        SCOPED_TRACE(shader);
        const auto material = parameter(shader, "materials");
        EXPECT_EQ(material.at("binding").at("index"), RenderInterface::kMaterialBufferBinding);
        EXPECT_EQ(material.at("binding").at("space"), RenderInterface::kMaterialSet);
        checkFields(material.at("type").at("resultType"), fields);
        checkFields(material.at("type").at("resultType").at("fields").at(16).at("type"), indices);
        const auto textures = parameter(shader, "textures");
        const auto samplers = parameter(shader, "samplers");
        EXPECT_EQ(textures.at("binding").at("index"), RenderInterface::kMaterialTexturesBinding);
        EXPECT_EQ(samplers.at("binding").at("index"), RenderInterface::kMaterialSamplersBinding);
        EXPECT_EQ(textures.at("binding").at("space"), RenderInterface::kMaterialSet);
        EXPECT_EQ(samplers.at("binding").at("space"), RenderInterface::kMaterialSet);
        EXPECT_EQ(textures.at("type").at("elementCount"), kMaxMaterialTextures);
        EXPECT_EQ(samplers.at("type").at("elementCount"), kMaxMaterialSamplers);
    }
    EXPECT_EQ(fields.back().offset + fields.back().size, sizeof(MaterialData));
}

TEST(ShaderDataLayout, SheenBindingsMatchCompiledShaders)
{
    for (const auto* shader : {"slang.sheen", "slang.sheen.no_ibl", "slang.clearcoat.sheen",
                               "slang.clearcoat.sheen.no_ibl", "deferred_lighting", "deferred_lighting.no_ibl"})
    {
        SCOPED_TRACE(shader);
        EXPECT_EQ(parameter(shader, "sheenLUT").at("binding").at("index"), RenderInterface::kSheenLutImageBinding);
        EXPECT_EQ(parameter(shader, "sheenLUTSampler").at("binding").at("index"), RenderInterface::kSheenLutSamplerBinding);
    }
}

TEST(ShaderDataLayout, PathTracingParametersMatchCompiledShaders)
{
    const std::array fields = {
        Field{"viewport", offsetof(PathTracingParameters, viewport), sizeof(glm::vec4)},
        Field{"lightRanges", offsetof(PathTracingParameters, lightRanges), sizeof(LightRanges)},
        Field{"maxBounces", offsetof(PathTracingParameters, maxBounces), sizeof(uint32_t)},
        Field{"sampleIndex", offsetof(PathTracingParameters, sampleIndex), sizeof(uint32_t)},
        Field{"iblEnabled", offsetof(PathTracingParameters, iblEnabled), sizeof(uint32_t)},
        Field{"pixelJitterEnabled", offsetof(PathTracingParameters, pixelJitterEnabled), sizeof(uint32_t)},
    };
    for (const auto* shader : {"raytracing"})
    {
        const auto parameters = parameter(shader, "parameters");
        checkFields(parameters.at("type").at("elementType"), fields);
        EXPECT_EQ(parameters.at("type").at("elementVarLayout").at("binding").at("size"), sizeof(PathTracingParameters));
    }
}

TEST(ShaderDataLayout, RayTracingHitDataMatchesCompiledShaders)
{
    const std::array fields = {
        Field{"vertexAddress", offsetof(RayTracingHitData, vertexAddress), sizeof(uint64_t)},
        Field{"indexAddress", offsetof(RayTracingHitData, indexAddress), sizeof(uint64_t)},
        Field{"firstIndex", offsetof(RayTracingHitData, firstIndex), sizeof(uint32_t)},
        Field{"materialIndex", offsetof(RayTracingHitData, materialIndex), sizeof(uint32_t)},
        Field{"padding", offsetof(RayTracingHitData, padding), sizeof(RayTracingHitData::padding)},
    };
    for (const auto* shader : {"raytracing"})
        checkFields(parameter(shader, "objects").at("type").at("resultType"), fields);
}

TEST(ShaderDataLayout, RayTracingBindingsMatchSharedDefinitions)
{
    const auto check = [](const char* shader, const char* name, uint32_t binding)
    {
        SCOPED_TRACE(shader);
        SCOPED_TRACE(name);
        const auto value = parameter(shader, name).at("binding");
        EXPECT_EQ(value.at("space"), RenderInterface::kRayTracingPassSet);
        EXPECT_EQ(value.at("index"), binding);
    };
    for (const auto* shader : {"raytracing", "raytracing.svgf"})
    {
        check(shader, "scene", RealtimeRayTracingBinding::kTlas);
        check(shader, "objects", RealtimeRayTracingBinding::kObjects);
        check(shader, "outputImage", RealtimeRayTracingBinding::kColor);
        check(shader, "depthImage", RealtimeRayTracingBinding::kDepth);
        check(shader, "normalImage", RealtimeRayTracingBinding::kNormal);
    }
    check("raytracing.svgf", "indirectImage", RealtimeRayTracingBinding::kIndirect);
    check("raytracing.svgf", "emissionImage", RealtimeRayTracingBinding::kEmission);
    check("raytracing.svgf", "albedoImage", RealtimeRayTracingBinding::kAlbedo);
    check("offline_pathtracing", "scene", OfflineRayTracingBinding::kTlas);
    check("offline_pathtracing", "objects", OfflineRayTracingBinding::kObjects);
    check("offline_pathtracing", "materialInstructions", OfflineRayTracingBinding::kMaterials);
    check("offline_pathtracing", "textureInstructions", OfflineRayTracingBinding::kTextures);
    check("offline_pathtracing", "areaEmissions", OfflineRayTracingBinding::kAreaEmissions);
    check("offline_pathtracing", "pointLights", OfflineRayTracingBinding::kPoints);
    check("offline_pathtracing", "spotLights", OfflineRayTracingBinding::kSpots);
    check("offline_pathtracing", "directionalLights", OfflineRayTracingBinding::kDirectionals);
    check("offline_pathtracing", "environmentLights", OfflineRayTracingBinding::kEnvironments);
    check("offline_pathtracing", "projectionLights", OfflineRayTracingBinding::kProjections);
    check("offline_pathtracing", "goniometricLights", OfflineRayTracingBinding::kGoniometrics);
    check("offline_pathtracing", "lightTriangles", OfflineRayTracingBinding::kTriangles);
    check("offline_pathtracing", "lightPortals", OfflineRayTracingBinding::kPortals);
    check("offline_pathtracing", "accumulation", OfflineRayTracingBinding::kAccumulation);
    check("offline_pathtracing", "outputImage", OfflineRayTracingBinding::kColor);
}

TEST(ShaderDataLayout, ToneMappingMatchesCompiledShader)
{
    const std::array expected = {
        Field{"exposureScale", offsetof(ToneMappingPushConstants, exposureScale), sizeof(float)},
        Field{"mode", offsetof(ToneMappingPushConstants, mode), sizeof(uint32_t)},
        Field{"encodeSrgb", offsetof(ToneMappingPushConstants, encodeSrgb), sizeof(uint32_t)},
        Field{"padding", offsetof(ToneMappingPushConstants, padding), sizeof(uint32_t)},
    };
    checkFields(parameter("tonemapping", "parameters").at("type").at("elementType"), expected);
    EXPECT_EQ(
        parameter("tonemapping", "hdrColor").at("binding").at("index"),
        RenderInterface::kToneMappingImageBinding
    );
    EXPECT_EQ(
        parameter("tonemapping", "hdrSampler").at("binding").at("index"),
        RenderInterface::kToneMappingSamplerBinding
    );
}

TEST(ShaderDataLayout, OfflineSceneUsesItsOwnBufferLayouts)
{
    const auto push = parameter("offline_pathtracing", "parameters");
    const std::array parameters = {
        Field{"viewport", offsetof(OfflinePathParameters, viewport), sizeof(glm::vec4)},
        Field{"samples", offsetof(OfflinePathParameters, samples), sizeof(glm::uvec4)},
        Field{"lightCounts", offsetof(OfflinePathParameters, lightCounts), sizeof(glm::uvec4)},
        Field{"mappedLightCounts", offsetof(OfflinePathParameters, mappedLightCounts), sizeof(glm::uvec4)},
    };
    checkFields(push.at("type").at("elementType"), parameters);
    EXPECT_EQ(push.at("type").at("elementVarLayout").at("binding").at("size"), sizeof(OfflinePathParameters));
    const auto check = [&](const char* name, uint32_t binding, std::span<const Field> fields)
    {
        const auto buffer = parameter("offline_pathtracing", name);
        EXPECT_EQ(buffer.at("binding").at("space"), RenderInterface::kRayTracingPassSet);
        EXPECT_EQ(buffer.at("binding").at("index"), binding);
        checkFields(buffer.at("type").at("resultType"), fields);
    };
    const std::array hit = {
        Field{"vertexAddress", offsetof(OfflineHitData, vertexAddress), sizeof(uint64_t)},
        Field{"indexAddress", offsetof(OfflineHitData, indexAddress), sizeof(uint64_t)},
        Field{"material", offsetof(OfflineHitData, material), sizeof(glm::uvec4)},
    };
    check("objects", OfflineRayTracingBinding::kObjects, hit);
    const std::array material = {
        Field{"model", offsetof(OfflineMaterialPreview, model), sizeof(OfflineMaterialPreviewModel)},
        Field{"firstTexture", offsetof(OfflineMaterialPreview, firstTexture), sizeof(uint32_t)},
        Field{"secondTexture", offsetof(OfflineMaterialPreview, secondTexture), sizeof(uint32_t)},
        Field{"instructionCount", offsetof(OfflineMaterialPreview, instructionCount), sizeof(uint32_t)},
        Field{"parameters", offsetof(OfflineMaterialPreview, parameters), sizeof(glm::vec4)},
    };
    check("materialInstructions", OfflineRayTracingBinding::kMaterials, material);
    const std::array texture = {
        Field{"metadata", offsetof(OfflineTextureInstruction, metadata), sizeof(glm::uvec4)},
        Field{"value", offsetof(OfflineTextureInstruction, value), sizeof(glm::vec4)},
        Field{"uvScaleOffset", offsetof(OfflineTextureInstruction, uvScaleOffset), sizeof(glm::vec4)},
        Field{"uAxisKind", offsetof(OfflineTextureInstruction, uAxisKind), sizeof(glm::vec4)},
        Field{"vAxis", offsetof(OfflineTextureInstruction, vAxis), sizeof(glm::vec4)},
        Field{"settings", offsetof(OfflineTextureInstruction, settings), sizeof(glm::vec4)},
        Field{"textureFromWorld", offsetof(OfflineTextureInstruction, textureFromWorld), sizeof(glm::mat4)},
    };
    check("textureInstructions", OfflineRayTracingBinding::kTextures, texture);
    const std::array emission = {
        Field{"colorScale", offsetof(OfflineLightEmission, colorScale), sizeof(glm::vec4)},
        Field{"normalization", offsetof(OfflineLightEmission, normalization), sizeof(glm::vec4)},
        Field{"resources", offsetof(OfflineLightEmission, resources), sizeof(glm::uvec4)},
    };
    check("areaEmissions", OfflineRayTracingBinding::kAreaEmissions, emission);
    const std::array point = {
        Field{"position", offsetof(OfflinePointLight, position), sizeof(glm::vec3)},
        Field{"padding", offsetof(OfflinePointLight, padding), sizeof(uint32_t)},
        Field{"emission", offsetof(OfflinePointLight, emission), sizeof(OfflineLightEmission)},
    };
    check("pointLights", OfflineRayTracingBinding::kPoints, point);
    const std::array spot = {
        Field{"position", offsetof(OfflineSpotLight, position), sizeof(glm::vec3)},
        Field{"cosInner", offsetof(OfflineSpotLight, cosInner), sizeof(float)},
        Field{"direction", offsetof(OfflineSpotLight, direction), sizeof(glm::vec3)},
        Field{"cosOuter", offsetof(OfflineSpotLight, cosOuter), sizeof(float)},
        Field{"emission", offsetof(OfflineSpotLight, emission), sizeof(OfflineLightEmission)},
    };
    check("spotLights", OfflineRayTracingBinding::kSpots, spot);
    const std::array directional = {
        Field{"direction", offsetof(OfflineDirectionalLight, direction), sizeof(glm::vec3)},
        Field{"padding", offsetof(OfflineDirectionalLight, padding), sizeof(uint32_t)},
        Field{"emission", offsetof(OfflineDirectionalLight, emission), sizeof(OfflineLightEmission)},
    };
    check("directionalLights", OfflineRayTracingBinding::kDirectionals, directional);
    const std::array triangle = {
        Field{"p0Area", offsetof(OfflineLightTriangle, p0Area), sizeof(glm::vec4)},
        Field{"p1", offsetof(OfflineLightTriangle, p1), sizeof(glm::vec4)},
        Field{"p2", offsetof(OfflineLightTriangle, p2), sizeof(glm::vec4)},
        Field{"normal", offsetof(OfflineLightTriangle, normal), sizeof(glm::vec4)},
        Field{"uv01", offsetof(OfflineLightTriangle, uv01), sizeof(glm::vec4)},
        Field{"uv2", offsetof(OfflineLightTriangle, uv2), sizeof(glm::vec2)},
        Field{"emissionIndex", offsetof(OfflineLightTriangle, emissionIndex), sizeof(uint32_t)},
        Field{"twoSided", offsetof(OfflineLightTriangle, twoSided), sizeof(uint32_t)},
    };
    check("lightTriangles", OfflineRayTracingBinding::kTriangles, triangle);
    const std::array environment = {
        Field{"lightFromWorld", offsetof(OfflineEnvironmentLight, lightFromWorld), sizeof(glm::mat4)},
        Field{"firstPortal", offsetof(OfflineEnvironmentLight, firstPortal), sizeof(uint32_t)},
        Field{"portalCount", offsetof(OfflineEnvironmentLight, portalCount), sizeof(uint32_t)},
        Field{"padding0", offsetof(OfflineEnvironmentLight, padding0), sizeof(uint32_t)},
        Field{"padding1", offsetof(OfflineEnvironmentLight, padding1), sizeof(uint32_t)},
        Field{"emission", offsetof(OfflineEnvironmentLight, emission), sizeof(OfflineLightEmission)},
    };
    check("environmentLights", OfflineRayTracingBinding::kEnvironments, environment);
    const std::array projection = {
        Field{"lightFromWorld", offsetof(OfflineProjectionLight, lightFromWorld), sizeof(glm::mat4)},
        Field{"position", offsetof(OfflineProjectionLight, position), sizeof(glm::vec3)},
        Field{"fieldOfView", offsetof(OfflineProjectionLight, fieldOfView), sizeof(float)},
        Field{"emission", offsetof(OfflineProjectionLight, emission), sizeof(OfflineLightEmission)},
    };
    check("projectionLights", OfflineRayTracingBinding::kProjections, projection);
    const std::array goniometric = {
        Field{"lightFromWorld", offsetof(OfflineGoniometricLight, lightFromWorld), sizeof(glm::mat4)},
        Field{"position", offsetof(OfflineGoniometricLight, position), sizeof(glm::vec3)},
        Field{"padding", offsetof(OfflineGoniometricLight, padding), sizeof(uint32_t)},
        Field{"emission", offsetof(OfflineGoniometricLight, emission), sizeof(OfflineLightEmission)},
    };
    check("goniometricLights", OfflineRayTracingBinding::kGoniometrics, goniometric);
}

TEST(ShaderDataLayout, EditorShadersUseOnlyViewData)
{
    const std::array viewFields = {Field{"viewProjection", 0, sizeof(glm::mat4)}};
    for (const auto* shader : {"editor_light_marker", "editor_picking", "editor_depth"})
    {
        SCOPED_TRACE(shader);
        const auto view = parameter(shader, "editorView");
        EXPECT_EQ(view.at("binding").at("index"), EditorInterface::kViewUniformBinding);
        EXPECT_EQ(view.at("binding").value("space", 0), EditorInterface::kViewSet);
        checkFields(view.at("type").at("elementType"), viewFields);
        const auto path =
            std::filesystem::path(RENDERLAB_SHADER_BINARY_DIR) / (std::string(shader) + ".reflection.json");
        std::ifstream input{path};
        const auto    reflection  = json::parse(input, nullptr, false);
        size_t        descriptors = 0;
        for (const auto& value : reflection.at("parameters"))
            if (value.at("binding").at("kind") == "descriptorTableSlot")
                ++descriptors;
        EXPECT_EQ(descriptors, 1);
    }

    const std::array markerFields = {
        Field{"model", offsetof(EditorLightMarkerPushConstants, model), sizeof(glm::mat4)},
        Field{"color", offsetof(EditorLightMarkerPushConstants, color), sizeof(glm::vec4)},
    };
    checkFields(parameter("editor_light_marker", "pushConstants").at("type").at("elementType"), markerFields);
    const std::array pickingFields = {
        Field{"model", offsetof(EditorPickingPushConstants, model), sizeof(glm::mat4)},
        Field{"selectionId", offsetof(EditorPickingPushConstants, selectionId), sizeof(uint32_t)},
        Field{"padding0", offsetof(EditorPickingPushConstants, padding0), sizeof(uint32_t)},
        Field{"padding1", offsetof(EditorPickingPushConstants, padding1), sizeof(uint32_t)},
        Field{"padding2", offsetof(EditorPickingPushConstants, padding2), sizeof(uint32_t)},
    };
    checkFields(parameter("editor_picking", "pushConstants").at("type").at("elementType"), pickingFields);
}
