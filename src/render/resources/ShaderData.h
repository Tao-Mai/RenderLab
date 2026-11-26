#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/constants.hpp>

// Matches SceneUniforms in render/impl/Scene/Camera/CameraData.slang.
struct alignas(16) SceneUniforms
{
    glm::mat4 viewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};

    glm::vec3 cameraU{1.0f, 0.0f, 0.0f};
    float     padding0 = 0.0f;
    glm::vec3 cameraV{0.0f, 1.0f, 0.0f};
    float     padding1 = 0.0f;
    glm::vec3 cameraW{0.0f, 0.0f, -1.0f};
    float     padding2 = 0.0f;

    glm::mat4 view{1.0f};
};

// Matches the uniform fields of SceneData in render/impl/Scene/Scene.slang.
struct alignas(16) SceneGlobals
{
    SceneUniforms camera;
    uint32_t analyticLightCount = 0;
    uint32_t padding[3]{};
};

// GPU discriminator; values match render/impl/Scene/Lights/LightData.slang.
enum class GpuLightType : uint32_t
{
    Point       = 0,
    Directional = 1,
    RectArea    = 2,
};

// One std430 SSBO element, with a 80-byte array stride. CPU preparation removes disabled lights.
struct alignas(16) LightData
{
    glm::vec3    posW{0.0f};
    GpuLightType lightType = GpuLightType::Point;
    glm::vec3    dirW{0.0f, -1.0f, 0.0f};
    float        openingAngle = glm::pi<float>();

    glm::vec3 intensity{0.0f};  // Point radiant intensity, directional irradiance, or area-light radiance.
    float cosOpeningAngle = -1.0f;
    glm::vec2 areaSize{1.0f};
    float penumbraAngle = 0.0f;
    float padding0 = 0.0f;
    glm::vec3 areaRight{1.0f, 0.0f, 0.0f};
    float padding1 = 0.0f;

    bool operator==(const LightData&) const = default;
};

static_assert(sizeof(SceneUniforms) == 192);
static_assert(offsetof(SceneUniforms, cameraPosition) == 64);
static_assert(offsetof(SceneUniforms, cameraU) == 80);
static_assert(offsetof(SceneUniforms, cameraV) == 96);
static_assert(offsetof(SceneUniforms, cameraW) == 112);
static_assert(offsetof(SceneUniforms, view) == 128);
static_assert(sizeof(SceneGlobals) == 208);
static_assert(offsetof(SceneGlobals, analyticLightCount) == 192);
static_assert(sizeof(LightData) == 80);
static_assert(offsetof(LightData, posW) == 0);
static_assert(offsetof(LightData, lightType) == 12);
static_assert(offsetof(LightData, dirW) == 16);
static_assert(offsetof(LightData, openingAngle) == 28);
static_assert(offsetof(LightData, intensity) == 32);
static_assert(offsetof(LightData, cosOpeningAngle) == 44);
static_assert(offsetof(LightData, areaSize) == 48);
static_assert(offsetof(LightData, penumbraAngle) == 56);
static_assert(offsetof(LightData, areaRight) == 64);
