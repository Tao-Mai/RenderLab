#include <vulkan/vulkan.hpp>

#include "core/Logger.h"
#include "render/feature/editor/EditorShaderData.h"
#include "render/raytracing/RayTracingData.h"
#include "render/resource/MaterialData.h"
#include "render/resource/ShaderData.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <utility>

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

constexpr std::array shaders = {
    "slang",
    "slang.no_ibl",
    "slang.clearcoat",
    "slang.clearcoat.no_ibl",
    "depth",
    "shadow",
    "shadow.rect_area",
    "shadow.directional",
    "shadow.spot",
    "skybox"
};
}  // namespace

TEST(ShaderDataLayout, ViewUniformsMatchCompiledShaders)
{
    const std::array expected = {
        Field{"viewProjection", offsetof(ViewUniforms, viewProjection), sizeof(glm::mat4)},
        Field{"inverseViewProjection", offsetof(ViewUniforms, inverseViewProjection), sizeof(glm::mat4)},
        Field{"cameraPosition", offsetof(ViewUniforms, cameraPosition), sizeof(glm::vec4)},
        Field{"view", offsetof(ViewUniforms, view), sizeof(glm::mat4)},
    };
    for (const auto* shader : shaders)
    {
        SCOPED_TRACE(shader);
        const auto value = parameter(shader, "viewUniforms");
        EXPECT_EQ(value.at("binding").at("index").get<uint32_t>(), RenderInterface::kViewUniformBinding);
        const auto& type = value.at("type");
        EXPECT_EQ(type.at("kind"), "constantBuffer");
        checkFields(type.at("elementType"), expected);
        EXPECT_EQ(type.at("elementVarLayout").at("binding").at("size").get<size_t>(), sizeof(ViewUniforms));
    }
}

TEST(ShaderDataLayout, LightUniformsMatchCompiledShaders)
{
    const std::array expected = {
        Field{"lightRanges", offsetof(LightUniforms, lightRanges), sizeof(LightRanges)},
        Field{"ambientCoefficient", offsetof(LightUniforms, ambientCoefficient), sizeof(float)},
        Field{"cascadeCount", offsetof(LightUniforms, cascadeCount), sizeof(uint32_t)},
        Field{"directionalShadowSampleRadius", offsetof(LightUniforms, directionalShadowSampleRadius), sizeof(float)},
        Field{"padding", offsetof(LightUniforms, padding), sizeof(float)},
        Field{"iblParameters", offsetof(LightUniforms, iblParameters), sizeof(glm::vec4)},
        Field{"shadowParameters", offsetof(LightUniforms, shadowParameters), sizeof(glm::vec4)},
    };
    for (const auto* shader : shaders)
    {
        SCOPED_TRACE(shader);
        const auto value = parameter(shader, "lightUniforms");
        EXPECT_EQ(value.at("binding").at("index").get<uint32_t>(), RenderInterface::kLightUniformBinding);
        const auto& type = value.at("type");
        EXPECT_EQ(type.at("kind"), "constantBuffer");
        checkFields(type.at("elementType"), expected);
        EXPECT_EQ(type.at("elementVarLayout").at("binding").at("size").get<size_t>(), sizeof(LightUniforms));
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
        Field{"shadowIdx", offsetof(LightData, shadowidx), sizeof(uint32_t)},
        Field{"padding0", offsetof(LightData, padding0), sizeof(uint32_t)},
        Field{"areaRightData", offsetof(LightData, areaRightData), sizeof(glm::vec4)},
    };
    for (const auto* shader : shaders)
    {
        SCOPED_TRACE(shader);
        const auto value = parameter(shader, "lights");
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
        EXPECT_EQ(shadow.at("binding").at("index").get<uint32_t>(), RenderInterface::kRectAreaShadowBufferBinding);
        checkFields(shadow.at("type").at("resultType"), expected);
    }
}

TEST(ShaderDataLayout, SceneTextureAndSamplerBindingsMatchCompiledShaders)
{
    const std::array bindings = {
        std::pair{"pointShadowMap", RenderInterface::kPointShadowImageBinding},
        std::pair{"spotShadowMap", RenderInterface::kSpotShadowImageBinding},
        std::pair{"rectAreaShadowMap", RenderInterface::kRectAreaShadowImageBinding},
        std::pair{"cascadeShadowMap", RenderInterface::kDirectionalShadowImageBinding},
        std::pair{"shadowSampler", RenderInterface::kShadowComparisonSamplerBinding},
        std::pair{"linearSampler", RenderInterface::kLinearSamplerBinding},
        std::pair{"irradianceMap", RenderInterface::kIrradianceImageBinding},
        std::pair{"irradianceMapSampler", RenderInterface::kIrradianceSamplerBinding},
        std::pair{"prefilteredSpecularMap", RenderInterface::kPrefilteredSpecularImageBinding},
        std::pair{"prefilteredSpecularMapSampler", RenderInterface::kPrefilteredSpecularSamplerBinding},
        std::pair{"brdfLUT", RenderInterface::kBrdfLutImageBinding},
        std::pair{"brdfLUTSampler", RenderInterface::kBrdfLutSamplerBinding},
    };
    for (const auto* shader : {"slang", "slang.no_ibl", "slang.clearcoat", "slang.clearcoat.no_ibl"})
    {
        SCOPED_TRACE(shader);
        for (const auto& [name, binding] : bindings)
        {
            SCOPED_TRACE(name);
            EXPECT_EQ(parameter(shader, name).at("binding").at("index").get<uint32_t>(), binding);
        }
    }
    EXPECT_EQ(
        parameter("skybox", "radianceMap").at("binding").at("index").get<uint32_t>(),
        RenderInterface::kRadianceImageBinding
    );
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
          "raytracing",
          "offline_pathtracing"})
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

TEST(ShaderDataLayout, RayTracingHitDataMatchesCompiledShaders)
{
    const std::array fields = {
        Field{"vertexAddress", offsetof(RayTracingHitData, vertexAddress), sizeof(uint64_t)},
        Field{"indexAddress", offsetof(RayTracingHitData, indexAddress), sizeof(uint64_t)},
        Field{"firstIndex", offsetof(RayTracingHitData, firstIndex), sizeof(uint32_t)},
        Field{"materialIndex", offsetof(RayTracingHitData, materialIndex), sizeof(uint32_t)},
        Field{"padding", offsetof(RayTracingHitData, padding), sizeof(RayTracingHitData::padding)},
    };
    for (const auto* shader : {"raytracing", "offline_pathtracing"})
        checkFields(parameter(shader, "objects").at("type").at("resultType"), fields);
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
