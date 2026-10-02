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

inline constexpr uint32_t viewUniformBinding = 0;
inline constexpr uint32_t lightUniformBinding = 11;
inline constexpr uint32_t lightBufferBinding = 12;
inline constexpr uint32_t irradianceImageBinding = 1;
inline constexpr uint32_t irradianceSamplerBinding = 2;
inline constexpr uint32_t prefilteredSpecularImageBinding = 3;
inline constexpr uint32_t prefilteredSpecularSamplerBinding = 4;
inline constexpr uint32_t brdfLutImageBinding = 5;
inline constexpr uint32_t brdfLutSamplerBinding = 6;
inline constexpr uint32_t radianceImageBinding = 7;
inline constexpr uint32_t radianceSamplerBinding = 8;
inline constexpr uint32_t shadowImageBinding = 9;
inline constexpr uint32_t shadowSamplerBinding = 10;
inline constexpr uint32_t materialImageBinding = 0;
inline constexpr uint32_t materialSamplerBinding = 1;
inline constexpr uint32_t materialUniformBinding = 2;
}

// Matches the std140 ViewUniforms block in shaders/scene_data.slang.
struct alignas(16) ViewUniforms
{
    glm::mat4 viewProjection{1.0f};
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
};

// The SSBO contains lightCount entries, including disabled lights. IBL.x is
// the maximum LOD of the scene's prefiltered specular cubemap.
struct alignas(16) LightUniforms
{
    uint32_t lightCount = 0;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
    glm::vec4 iblParameters{0.0f};
};

// One std430 SSBO element: five 16-byte vectors, with an 80-byte array stride.
// flags = {light type, enabled, castShadow, reserved}.
struct alignas(16) LightData
{
    glm::vec4 colorIntensity{1.0f, 1.0f, 1.0f, 0.0f};
    glm::vec4 positionRange{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 direction{0.0f, -1.0f, 0.0f, 0.0f};
    glm::vec4 areaSizeCone{1.0f, 1.0f, 0.9396926f, 0.8660254f};
    glm::uvec4 flags{0u};
};

struct alignas(16) MaterialUniforms
{
    glm::vec4 baseColorFactor{1.0f};
    float roughness = 1.0f;
    float metallic = 1.0f;
    float alphaCutoff = 0.5f;
    uint32_t alphaMode = 0;
};

static_assert(sizeof(ViewUniforms) == 144);
static_assert(offsetof(ViewUniforms, inverseViewProjection) == 64);
static_assert(offsetof(ViewUniforms, cameraPosition) == 128);
static_assert(sizeof(LightUniforms) == 32);
static_assert(offsetof(LightUniforms, iblParameters) == 16);
static_assert(sizeof(LightData) == 80);
static_assert(offsetof(LightData, positionRange) == 16);
static_assert(offsetof(LightData, direction) == 32);
static_assert(offsetof(LightData, areaSizeCone) == 48);
static_assert(offsetof(LightData, flags) == 64);
static_assert(sizeof(MaterialUniforms) == 32);
static_assert(offsetof(MaterialUniforms, roughness) == 16);
static_assert(offsetof(MaterialUniforms, metallic) == 20);
static_assert(offsetof(MaterialUniforms, alphaCutoff) == 24);
static_assert(offsetof(MaterialUniforms, alphaMode) == 28);

struct MeshPushConstants
{
    glm::mat4 model{1.0f};
};

struct ShadowPushConstants
{
    glm::mat4 model{1.0f};
    glm::mat4 viewProjection{1.0f};
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

struct SkyboxPushConstants
{
    glm::vec4 viewport{0.0f};
};

static_assert(sizeof(MeshPushConstants) == 64);
static_assert(sizeof(ShadowPushConstants) == 128);
static_assert(sizeof(LightPushConstants) == 80);
static_assert(sizeof(EditorPickingPushConstants) == 80);
static_assert(sizeof(SkyboxPushConstants) == 16);
