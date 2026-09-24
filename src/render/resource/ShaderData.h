#pragma once

#include <cstdint>
#include <cstddef>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace RenderInterface
{
inline constexpr uint32_t sceneSet = 0;
inline constexpr uint32_t materialSet = 1;
inline constexpr uint32_t objectSet = 2;
inline constexpr uint32_t passSet = 3;

inline constexpr uint32_t sceneUniformBinding = 0;
inline constexpr uint32_t environmentImageBinding = 1;
inline constexpr uint32_t environmentSamplerBinding = 2;
inline constexpr uint32_t materialImageBinding = 0;
inline constexpr uint32_t materialSamplerBinding = 1;
inline constexpr uint32_t materialUniformBinding = 2;
}

struct SceneUniforms
{
    glm::mat4  viewProjection{1.0f};
    glm::vec4  lightColorIntensity{1.0f, 1.0f, 1.0f, 0.0f};
    glm::vec4  lightPositionRange{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4  lightDirection{0.0f, -1.0f, 0.0f, 0.0f};
    glm::vec4  lightAreaSizeCone{1.0f, 1.0f, 0.9396926f, 0.8660254f};
    glm::uvec4 lightFlags{0u};
    glm::vec4  cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
};

struct alignas(16) MaterialUniforms
{
    glm::vec4 baseColorFactor{1.0f};
    float roughness = 1.0f;
    float metallic = 1.0f;
    float alphaCutoff = 0.5f;
    uint32_t alphaMode = 0;
};

static_assert(sizeof(SceneUniforms) == 160);
static_assert(offsetof(SceneUniforms, lightColorIntensity) == 64);
static_assert(offsetof(SceneUniforms, lightPositionRange) == 80);
static_assert(offsetof(SceneUniforms, lightDirection) == 96);
static_assert(offsetof(SceneUniforms, lightAreaSizeCone) == 112);
static_assert(offsetof(SceneUniforms, lightFlags) == 128);
static_assert(offsetof(SceneUniforms, cameraPosition) == 144);
static_assert(sizeof(MaterialUniforms) == 32);
static_assert(offsetof(MaterialUniforms, roughness) == 16);
static_assert(offsetof(MaterialUniforms, metallic) == 20);
static_assert(offsetof(MaterialUniforms, alphaCutoff) == 24);
static_assert(offsetof(MaterialUniforms, alphaMode) == 28);

struct MeshPushConstants
{
    glm::mat4 model{1.0f};
};

struct LightPushConstants
{
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

struct EditorPickingPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t selectionId = 0;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};

static_assert(sizeof(MeshPushConstants) == 64);
static_assert(sizeof(LightPushConstants) == 80);
static_assert(sizeof(EditorPickingPushConstants) == 80);
