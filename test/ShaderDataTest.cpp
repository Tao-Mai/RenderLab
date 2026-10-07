#include "render/resource/ShaderData.h"
#include "core/Logger.h"

#include <array>
#include <filesystem>
#include <fstream>
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
    size_t offset;
    size_t size;
};

json parameter(std::string_view shader, std::string_view name)
{
    const auto path = std::filesystem::path(RENDERLAB_SHADER_BINARY_DIR) /
        (std::string(shader) + ".reflection.json");
    std::ifstream input{path};
    CHECK(input.is_open(), "cannot open shader reflection: {}", path.string());
    const auto reflection = json::parse(input, nullptr, false);
    CHECK(!reflection.is_discarded(), "invalid shader reflection: {}", path.string());
    for (const auto& value : reflection.at("parameters").get_ref<const json::array_t&>())
        if (value.at("name").get<std::string>() == name) return value;
    CHECK(false, "missing shader parameter: {}", name);
}

template <size_t N>
void checkFields(const json& type, const std::array<Field, N>& expected)
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
    "slang", "slang.no_ibl", "light", "editor_picking", "shadow", "shadow.rect_area", "skybox"
};
}

TEST(ShaderDataLayout, ViewUniformsMatchCompiledShaders)
{
    const std::array expected = {
        Field{"viewProjection", offsetof(ViewUniforms, viewProjection), sizeof(glm::mat4)},
        Field{"inverseViewProjection", offsetof(ViewUniforms, inverseViewProjection), sizeof(glm::mat4)},
        Field{"cameraPosition", offsetof(ViewUniforms, cameraPosition), sizeof(glm::vec4)},
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
        Field{"lightCount", offsetof(LightUniforms, lightCount), sizeof(uint32_t)},
        Field{"ambientCoefficient", offsetof(LightUniforms, ambientCoefficient), sizeof(float)},
        Field{"padding1", offsetof(LightUniforms, padding1), sizeof(uint32_t)},
        Field{"padding2", offsetof(LightUniforms, padding2), sizeof(uint32_t)},
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
        Field{"colorIntensity", offsetof(LightData, colorIntensity), sizeof(glm::vec4)},
        Field{"positionRange", offsetof(LightData, positionRange), sizeof(glm::vec4)},
        Field{"direction", offsetof(LightData, direction), sizeof(glm::vec4)},
        Field{"areaSizeCone", offsetof(LightData, areaSizeCone), sizeof(glm::vec4)},
        Field{"flags", offsetof(LightData, flags), sizeof(glm::uvec4)},
        Field{"shadowIdx", offsetof(LightData, shadowidx), sizeof(uint32_t)},
        Field{"padding0", offsetof(LightData, padding0), sizeof(uint32_t)},
        Field{"padding1", offsetof(LightData, padding1), sizeof(uint32_t)},
        Field{"padding2", offsetof(LightData, padding2), sizeof(uint32_t)},
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

    const auto push = parameter("shadow", "pushConstants");
    const std::array pushFields = {
        Field{"model", offsetof(ShadowPushConstants, model), sizeof(glm::mat4)},
        Field{"lightIdx", offsetof(ShadowPushConstants, lightIdx), sizeof(uint32_t)},
        Field{"padding0", offsetof(ShadowPushConstants, padding0), sizeof(uint32_t)},
        Field{"padding1", offsetof(ShadowPushConstants, padding1), sizeof(uint32_t)},
        Field{"padding2", offsetof(ShadowPushConstants, padding2), sizeof(uint32_t)},
    };
    checkFields(push.at("type").at("elementType"), pushFields);
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

TEST(ShaderDataLayout, TextureAndSamplerBindingsMatchCompiledShaders)
{
    const std::array bindings = {
        std::pair{"pointShadowMap", RenderInterface::kPointShadowImageBinding},
        std::pair{"rectAreaShadowMap", RenderInterface::kRectAreaShadowImageBinding},
        std::pair{"shadowSampler", RenderInterface::kShadowComparisonSamplerBinding},
        std::pair{"linearSampler", RenderInterface::kLinearSamplerBinding},
        std::pair{"irradianceMap", RenderInterface::kIrradianceImageBinding},
        std::pair{"irradianceMapSampler", RenderInterface::kIrradianceSamplerBinding},
        std::pair{"prefilteredSpecularMap", RenderInterface::kPrefilteredSpecularImageBinding},
        std::pair{"prefilteredSpecularMapSampler", RenderInterface::kPrefilteredSpecularSamplerBinding},
        std::pair{"brdfLUT", RenderInterface::kBrdfLutImageBinding},
        std::pair{"brdfLUTSampler", RenderInterface::kBrdfLutSamplerBinding},
        std::pair{"baseColorTexture", RenderInterface::kMaterialImageBinding},
        std::pair{"baseColorSampler", RenderInterface::kMaterialSamplerBinding},
        std::pair{"material", RenderInterface::kMaterialUniformBinding},
    };
    for (const auto* shader : {"slang", "slang.no_ibl"})
    {
        SCOPED_TRACE(shader);
        for (const auto& [name, binding] : bindings)
        {
            SCOPED_TRACE(name);
            EXPECT_EQ(parameter(shader, name).at("binding").at("index").get<uint32_t>(), binding);
        }
    }
    EXPECT_EQ(parameter("skybox", "radianceMap").at("binding").at("index").get<uint32_t>(),
              RenderInterface::kRadianceImageBinding);
    EXPECT_EQ(parameter("skybox", "radianceSampler").at("binding").at("index").get<uint32_t>(),
              RenderInterface::kRadianceSamplerBinding);
}
