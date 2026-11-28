#pragma once

#include <slang-rhi.h>
#include "core/Vertex.h"

#include <cstddef>
#include <cstdint>

#include <glm/gtc/constants.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// Matches GeometryType in render/impl/Scene/SceneTypes.slang.
enum class GeometryType : uint32_t
{
    None         = 0,
    TriangleMesh = 1,
};

// Matches GeometryInstanceFlags in render/impl/Scene/SceneTypes.slang.
enum class GeometryInstanceFlags : uint32_t
{
    None                = 0x0,
    TransformFlipped    = 0x4,
    IsObjectFrontFaceCW = 0x8,
    IsWorldFrontFaceCW  = 0x10,
};
SLANG_RHI_ENUM_CLASS_OPERATORS(GeometryInstanceFlags);

// Matches GeometryInstanceData in render/impl/Scene/SceneTypes.slang (32 bytes).
struct GeometryInstanceData
{
    static constexpr uint32_t kTypeBits   = 3;
    static constexpr uint32_t kTypeOffset = 32 - kTypeBits;

    uint32_t flags          = 0;
    uint32_t globalMatrixID = 0;
    uint32_t materialID     = 0;
    uint32_t geometryID     = 0;
    uint32_t vbOffset       = 0;
    uint32_t ibOffset       = 0;
    uint32_t instanceIndex  = 0;
    uint32_t geometryIndex  = 0;
};

// Matches CameraData in render/impl/Scene/Camera/CameraData.slang; column-major storage.
struct alignas(16) CameraData
{
    glm::mat4 viewMat{1.0f};
    glm::mat4 viewProjMat{1.0f};

    glm::vec3 posW{0.0f};
    float     padding0 = 0.0f;
    glm::vec3 cameraU{1.0f, 0.0f, 0.0f};
    float     padding1 = 0.0f;
    glm::vec3 cameraV{0.0f, 1.0f, 0.0f};
    float     padding2 = 0.0f;
    glm::vec3 cameraW{0.0f, 0.0f, -1.0f};
    float     padding3 = 0.0f;
};

// GPU discriminator; values match LightType in render/impl/Scene/Lights/LightData.slang.
enum class LightType : uint32_t
{
    Point       = 0,
    Directional = 1,
    Rect        = 2,
};

// One std430 SSBO element (192 bytes), matching LightData in render/impl/Scene/Lights/LightData.slang.
struct alignas(16) LightData
{
    glm::vec3 posW{0.0f};
    LightType type = LightType::Point;
    glm::vec3 dirW{0.0f, -1.0f, 0.0f};
    float     openingAngle = glm::pi<float>();
    glm::vec3 intensity{0.0f};  // Point radiant intensity, directional irradiance, or area-light radiance.
    float     cosOpeningAngle = -1.0f;
    float     penumbraAngle   = 0.0f;
    float     surfaceArea     = 0.0f;
    glm::vec2 pad0{0.0f};

    glm::mat4 transMat{1.0f};
    glm::mat4 transMatIT{1.0f};

    bool operator==(const LightData&) const = default;
};

static_assert(sizeof(Vertex) == 48);
static_assert(offsetof(Vertex, position) == 0);
static_assert(offsetof(Vertex, normal) == 12);
static_assert(offsetof(Vertex, texcoord) == 24);
static_assert(offsetof(Vertex, tangent) == 32);

static_assert(sizeof(GeometryInstanceData) == 32);

static_assert(sizeof(CameraData) == 192);
static_assert(offsetof(CameraData, viewMat) == 0);
static_assert(offsetof(CameraData, viewProjMat) == 64);
static_assert(offsetof(CameraData, posW) == 128);
static_assert(offsetof(CameraData, cameraU) == 144);
static_assert(offsetof(CameraData, cameraV) == 160);
static_assert(offsetof(CameraData, cameraW) == 176);

static_assert(sizeof(LightData) == 192);
static_assert(offsetof(LightData, posW) == 0);
static_assert(offsetof(LightData, type) == 12);
static_assert(offsetof(LightData, dirW) == 16);
static_assert(offsetof(LightData, openingAngle) == 28);
static_assert(offsetof(LightData, intensity) == 32);
static_assert(offsetof(LightData, cosOpeningAngle) == 44);
static_assert(offsetof(LightData, penumbraAngle) == 48);
static_assert(offsetof(LightData, surfaceArea) == 52);
static_assert(offsetof(LightData, transMat) == 64);
static_assert(offsetof(LightData, transMatIT) == 128);
