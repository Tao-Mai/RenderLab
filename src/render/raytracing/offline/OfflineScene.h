#pragma once

#include "render/DrawItem.h"
#include "render/raytracing/offline/OfflineLightData.h"
#include "render/raytracing/offline/OfflineShaderTypes.h"

#include <span>

class AssetManager;
class MaterialTable;
class RenderResourceManager;

struct alignas(16) OfflineTextureInstruction
{
    glm::uvec4 metadata{}; // operation, scalar output / program length, texture, sampler.
    glm::vec4 value{};
    glm::vec4 uvScaleOffset{1, 1, 0, 0};
    glm::vec4 uAxisKind{1, 0, 0, 0};
    glm::vec4 vAxis{0, 1, 0, 0};
    glm::vec4 settings{};
    glm::mat4 textureFromWorld{1};
};
static_assert(sizeof(OfflineTextureInstruction) == 160);

struct alignas(16) OfflineMaterialPreview
{
    OfflineMaterialPreviewModel model = OfflineMaterialPreviewModel::Diffuse;
    uint32_t firstTexture = 0;
    uint32_t secondTexture = 0;
    uint32_t instructionCount = 0;
    glm::vec4 parameters{};
};
static_assert(sizeof(OfflineMaterialPreview) == 32);

struct alignas(16) OfflineHitData
{
    uint64_t vertexAddress = 0, indexAddress = 0;
    glm::uvec4 material{}; // first index, material index, reverse orientation, first area light.
};
static_assert(sizeof(OfflineHitData) == 32);

struct alignas(16) OfflinePathParameters
{
    glm::vec4 viewport{};
    glm::uvec4 samples{}; // accumulated samples, max bounces, reserved, reserved.
    glm::uvec4 lightCounts{}; // points, spots, directionals, triangles.
    glm::uvec4 mappedLightCounts{}; // hasEnvironment (0/1), projections, goniometrics, portal vertices.
};
static_assert(sizeof(OfflinePathParameters) == 64);

struct alignas(16) OfflineLightEmission
{
    glm::vec4 colorScale{}; // RGB preview, scale.
    glm::vec4 normalization{}; // source power in lm, illuminance in lx, total source area, reserved.
    glm::uvec4 resources{0, 0, kOfflineInvalidIndex, 0}; // texture, sampler, medium, flags.
};
static_assert(sizeof(OfflineLightEmission) == 48);

struct alignas(16) OfflinePointLight
{
    glm::vec3 position{};
    uint32_t padding = 0;
    OfflineLightEmission emission;
};
static_assert(sizeof(OfflinePointLight) == 64);

struct alignas(16) OfflineSpotLight
{
    glm::vec3 position{};
    float cosInner = 0;
    glm::vec3 direction{};
    float cosOuter = 0;
    OfflineLightEmission emission;
};
static_assert(sizeof(OfflineSpotLight) == 80);

struct alignas(16) OfflineDirectionalLight
{
    glm::vec3 direction{};
    uint32_t padding = 0;
    OfflineLightEmission emission;
};
static_assert(sizeof(OfflineDirectionalLight) == 64);

struct alignas(16) OfflineLightTriangle
{
    glm::vec4 p0Area{};
    glm::vec4 p1{};
    glm::vec4 p2{};
    glm::vec4 normal{};
    glm::vec4 uv01{};
    glm::vec2 uv2{};
    uint32_t emissionIndex = 0;
    uint32_t twoSided = 0;
};
static_assert(sizeof(OfflineLightTriangle) == 96);

struct alignas(16) OfflineEnvironmentLight
{
    glm::mat4 lightFromWorld{1};
    uint32_t firstPortal = 0;
    uint32_t portalCount = 0;
    uint32_t padding0 = 0, padding1 = 0;
    OfflineLightEmission emission;
};
static_assert(sizeof(OfflineEnvironmentLight) == 128);

struct alignas(16) OfflineProjectionLight
{
    glm::mat4 lightFromWorld{1};
    glm::vec3 position{};
    float fieldOfView = 0;
    OfflineLightEmission emission;
};
static_assert(sizeof(OfflineProjectionLight) == 128);

struct alignas(16) OfflineGoniometricLight
{
    glm::mat4 lightFromWorld{1};
    glm::vec3 position{};
    uint32_t padding = 0;
    OfflineLightEmission emission;
};
static_assert(sizeof(OfflineGoniometricLight) == 128);

struct OfflineDrawData
{
    uint32_t materialIndex = 0;
    uint32_t reverseOrientation = 0;
    uint32_t firstAreaLight = kOfflineInvalidIndex;
};

struct OfflineSceneData
{
    uint64_t revision = 0;
    std::vector<OfflineMaterialPreview> materials;
    std::vector<OfflineTextureInstruction> textures;
    std::vector<OfflineDrawData> draws;
    std::vector<OfflineLightEmission> areaEmissions;
    std::vector<OfflinePointLight> pointLights;
    std::vector<OfflineSpotLight> spotLights;
    std::vector<OfflineDirectionalLight> directionalLights;
    std::vector<OfflineLightTriangle> lightTriangles;
    OfflineEnvironmentLight environmentLight{};
    bool hasEnvironmentLight = false;
    std::vector<OfflineProjectionLight> projectionLights;
    std::vector<OfflineGoniometricLight> goniometricLights;
    std::vector<glm::vec4> lightPortals;
    std::vector<AssetID<PbrtMediumAsset>> lightMedia;
};

class OfflineScene
{
public:
    const OfflineSceneData& prepare(std::span<const DrawItem> items, std::span<const OfflineLightData> lights,
        AssetManager& assets, RenderResourceManager& resources, MaterialTable& materialTable);

private:
    OfflineSceneData data;
    uint64_t previousAssetRevision = ~uint64_t{0};
    std::vector<DrawItem> previousItems;
    std::vector<OfflineLightData> previousLights;
};
