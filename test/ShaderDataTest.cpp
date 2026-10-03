#include "render/resource/ShaderData.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

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
    if (!input.is_open()) throw std::runtime_error("cannot open shader reflection: " + path.string());
    const auto reflection = json::parse(input);
    for (const auto& value : reflection.at("parameters").get_ref<const json::array_t&>())
        if (value.at("name").get<std::string>() == name) return value;
    throw std::runtime_error("missing shader parameter: " + std::string(name));
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

constexpr std::array shaders = {"slang", "light", "editor_picking", "shadow", "skybox"};
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
        EXPECT_EQ(value.at("binding").at("index").get<uint32_t>(), RenderInterface::viewUniformBinding);
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
        Field{"padding0", offsetof(LightUniforms, padding0), sizeof(uint32_t)},
        Field{"padding1", offsetof(LightUniforms, padding1), sizeof(uint32_t)},
        Field{"padding2", offsetof(LightUniforms, padding2), sizeof(uint32_t)},
        Field{"iblParameters", offsetof(LightUniforms, iblParameters), sizeof(glm::vec4)},
    };
    for (const auto* shader : shaders)
    {
        SCOPED_TRACE(shader);
        const auto value = parameter(shader, "lightUniforms");
        EXPECT_EQ(value.at("binding").at("index").get<uint32_t>(), RenderInterface::lightUniformBinding);
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
    };
    for (const auto* shader : shaders)
    {
        SCOPED_TRACE(shader);
        const auto value = parameter(shader, "lights");
        EXPECT_EQ(value.at("binding").at("index").get<uint32_t>(), RenderInterface::lightBufferBinding);
        const auto& type = value.at("type");
        EXPECT_EQ(type.at("baseShape"), "structuredBuffer");
        checkFields(type.at("resultType"), expected);
        EXPECT_EQ(expected.back().offset + expected.back().size, sizeof(LightData));
    }
}
