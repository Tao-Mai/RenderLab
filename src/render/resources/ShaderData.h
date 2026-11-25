#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

// Matches SceneUniforms in render/impl/scene/scene_uniforms.slang.
struct alignas(16) SceneUniforms
{
    glm::mat4 viewProjection{1.0f};
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
    glm::mat4 view{1.0f};
};

// Matches the uniform fields of SceneData in render/impl/scene/scene_data.slang.
struct alignas(16) SceneGlobals
{
    SceneUniforms camera;
    uint32_t analyticLightCount = 0;
    uint32_t padding[3]{};
};

// GPU discriminator; values match render/impl/scene/light_data.slang.
enum class GpuLightType : uint32_t
{
    Point       = 0,
    Directional = 1,
    RectArea    = 2,
    Spot        = 3,
};

// One std430 SSBO element, with a 80-byte array stride. CPU preparation removes disabled lights.
struct alignas(16) LightData
{
    bool operator==(const LightData&) const = default;
    // Photometric-scaled RGB: Point/Spot intensity, Directional irradiance, RectArea radiance.
    glm::vec3    color{0.0f};
    GpuLightType lightType = GpuLightType::Point;
    glm::vec4    positionRange{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4    direction{0.0f, -1.0f, 0.0f, 0.0f};
    glm::vec4    areaSizeCone{1.0f, 1.0f, 0.9396926f, 0.8660254f};
    glm::vec4    areaRightData{1.0f, 0.0f, 0.0f, 0.0f};

    [[nodiscard]] glm::vec3 areaRight() const { return xyz(areaRightData); }

    [[nodiscard]] glm::vec3 position() const { return xyz(positionRange); }

    [[nodiscard]] float range() const { return positionRange.w; }

    [[nodiscard]] GpuLightType type() const { return lightType; }

};

static_assert(sizeof(SceneUniforms) == 208);
static_assert(offsetof(SceneUniforms, inverseViewProjection) == 64);
static_assert(offsetof(SceneUniforms, cameraPosition) == 128);
static_assert(offsetof(SceneUniforms, view) == 144);
static_assert(sizeof(SceneGlobals) == 224);
static_assert(offsetof(SceneGlobals, analyticLightCount) == 208);
static_assert(sizeof(LightData) == 80);
static_assert(offsetof(LightData, color) == 0);
static_assert(offsetof(LightData, lightType) == 12);
static_assert(offsetof(LightData, positionRange) == 16);
static_assert(offsetof(LightData, direction) == 32);
static_assert(offsetof(LightData, areaSizeCone) == 48);
static_assert(offsetof(LightData, areaRightData) == 64);
