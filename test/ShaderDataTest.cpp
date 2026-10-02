#include "render/resource/ShaderData.h"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string_view>

#include <gtest/gtest.h>
#include <rfl/json.hpp>

namespace
{
struct Field
{
    std::string_view name;
    size_t offset;
    size_t size;
};

rfl::Generic field(const rfl::Generic& value, std::string_view name)
{
    const auto object = value.to_object();
    if (!object) throw std::runtime_error("reflection field is not an object");
    const auto result = object->get(std::string(name));
    if (!result) throw std::runtime_error("missing reflection field: " + std::string(name));
    return *result;
}

rfl::Generic parameter(std::string_view shader, std::string_view name)
{
    const auto path = std::filesystem::path(RENDERLAB_SHADER_BINARY_DIR) /
        (std::string(shader) + ".reflection.json");
    const auto reflection = rfl::json::load<rfl::Generic>(path.string());
    if (!reflection) throw std::runtime_error(reflection.error().what());
    const auto parameters = field(*reflection, "parameters").to_array();
    if (!parameters) throw std::runtime_error("missing reflection parameters");
    for (const auto& value : *parameters)
        if (field(value, "name").to_string().value() == name) return value;
    throw std::runtime_error("missing shader parameter: " + std::string(name));
}

template <size_t N>
void checkFields(const rfl::Generic& type, const std::array<Field, N>& expected)
{
    const auto fields = field(type, "fields").to_array();
    ASSERT_TRUE(fields);
    ASSERT_EQ(fields->size(), expected.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
        SCOPED_TRACE(expected[index].name);
        const auto& value = fields->at(index);
        EXPECT_EQ(field(value, "name").to_string().value(), expected[index].name);
        const auto binding = field(value, "binding");
        EXPECT_EQ(field(binding, "offset").to_int().value(), expected[index].offset);
        EXPECT_EQ(field(binding, "size").to_int().value(), expected[index].size);
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
        EXPECT_EQ(field(field(value, "binding"), "index").to_int().value(), RenderInterface::viewUniformBinding);
        const auto type = field(value, "type");
        EXPECT_EQ(field(type, "kind").to_string().value(), "constantBuffer");
        checkFields(field(type, "elementType"), expected);
        EXPECT_EQ(field(field(field(type, "elementVarLayout"), "binding"), "size").to_int().value(), sizeof(ViewUniforms));
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
        EXPECT_EQ(field(field(value, "binding"), "index").to_int().value(), RenderInterface::lightUniformBinding);
        const auto type = field(value, "type");
        EXPECT_EQ(field(type, "kind").to_string().value(), "constantBuffer");
        checkFields(field(type, "elementType"), expected);
        EXPECT_EQ(field(field(field(type, "elementVarLayout"), "binding"), "size").to_int().value(), sizeof(LightUniforms));
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
        EXPECT_EQ(field(field(value, "binding"), "index").to_int().value(), RenderInterface::lightBufferBinding);
        const auto type = field(value, "type");
        EXPECT_EQ(field(type, "baseShape").to_string().value(), "structuredBuffer");
        checkFields(field(type, "resultType"), expected);
        EXPECT_EQ(expected.back().offset + expected.back().size, sizeof(LightData));
    }
}
