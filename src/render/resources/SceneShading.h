#pragma once


#include "render/resources/SceneLightConstants.h"
#include "render/resources/ShaderData.h"
#include "scene/system/SceneLight.h"

#include <cstdint>
#include <span>
#include <vector>

class AssetDataManager;
class MaterialTable;
struct GpuDraw;

inline constexpr uint32_t kMaxLights = 65536;

struct alignas(16) GpuLightEmission
{
    glm::vec4  colorScale{};     // RGB preview, scale.
    glm::vec4  normalization{};  // source power in lm, illuminance in lx, total source area, reserved.
    glm::uvec4 resources{0, 0, kInvalidLightIndex, 0};  // texture, sampler, medium, flags.
};
static_assert(sizeof(GpuLightEmission) == 48);

struct alignas(16) GpuLightTriangle
{
    glm::vec4 p0Area{};
    glm::vec4 p1{};
    glm::vec4 p2{};
    glm::vec4 normal{};
    glm::vec4 uv01{};
    glm::vec2 uv2{};
    uint32_t  emissionIndex = 0;
    uint32_t  twoSided      = 0;
};
static_assert(sizeof(GpuLightTriangle) == kLightTriangleStride);

struct alignas(16) GpuEnvironmentLight
{
    glm::mat4        lightFromWorld{1};
    uint32_t         firstPortal = 0;
    uint32_t         portalCount = 0;
    uint32_t         padding0 = 0, padding1 = 0;
    GpuLightEmission emission;
};
static_assert(sizeof(GpuEnvironmentLight) == 128);

struct alignas(16) GpuProjectionLight
{
    glm::mat4        lightFromWorld{1};
    glm::vec3        position{};
    float            fieldOfView = 0;
    GpuLightEmission emission;
};
static_assert(sizeof(GpuProjectionLight) == 128);

struct alignas(16) GpuGoniometricLight
{
    glm::mat4        lightFromWorld{1};
    glm::vec3        position{};
    uint32_t         padding = 0;
    GpuLightEmission emission;
};
static_assert(sizeof(GpuGoniometricLight) == 128);

struct SceneShadingData
{
    uint64_t                              revision = 0;
    std::vector<GpuLightEmission>         areaEmissions;
    std::vector<LightData>                lights;
    LightRanges                           lightRanges;
    std::vector<GpuLightTriangle>         lightTriangles;
    uint32_t                              firstRectTriangle = 0;
    GpuEnvironmentLight                   environmentLight{};
    bool                                  hasEnvironmentLight = false;
    std::vector<GpuProjectionLight>       projectionLights;
    std::vector<GpuGoniometricLight>      goniometricLights;
    std::vector<glm::vec4>                lightPortals;
    std::vector<AssetID<PbrtMediumAsset>> lightMedia;
};

class SceneShading
{
public:
    void update(
        std::span<const SceneLight> lights,
        const AssetDataManager&     assetData,
        MaterialTable&              materials
    );
    void buildEmissiveTriangles(
        std::span<GpuDraw>        draws,
        const AssetDataManager&   assetData,
        const MaterialTable&      materials
    );
    [[nodiscard]] const SceneShadingData& data() const { return shadingData; }

private:
    struct MeshEmitter
    {
        SceneLight::MeshArea mesh;
        GpuLightEmission     emission;
    };

    void prepareLights(std::span<const SceneLight> lights, const AssetDataManager& assetData, MaterialTable& materials);

    SceneShadingData         shadingData;
    std::vector<MeshEmitter> meshEmitters;
};
