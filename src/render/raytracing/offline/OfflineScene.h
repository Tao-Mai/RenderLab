#pragma once

#include "render/DrawItem.h"
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
    OfflineMaterialPreviewModel model = OfflineMaterialPreviewModel::Program;
    uint32_t firstTexture = 0;
    uint32_t secondTexture = 0;
    uint32_t instructionCount = 0;
    glm::vec4 parameters{};
};
static_assert(sizeof(OfflineMaterialPreview) == 32);

struct alignas(16) OfflineHitData
{
    uint64_t vertexAddress = 0, indexAddress = 0;
    glm::uvec4 material{}; // first index, material index, reverse orientation, reserved.
};
static_assert(sizeof(OfflineHitData) == 32);

struct alignas(16) OfflinePathParameters
{
    glm::vec4 viewport{};
    glm::uvec4 samples{}; // accumulated samples, pixel jitter, reserved, reserved.
};
static_assert(sizeof(OfflinePathParameters) == 32);

struct OfflineDrawData
{
    uint32_t materialIndex = 0;
    uint32_t reverseOrientation = 0;
};

struct OfflineSceneData
{
    uint64_t revision = 0;
    std::vector<OfflineMaterialPreview> materials;
    std::vector<OfflineTextureInstruction> textures;
    std::vector<OfflineDrawData> draws;
};

class OfflineScene
{
public:
    const OfflineSceneData& prepare(std::span<const DrawItem> items,
        AssetManager& assets, RenderResourceManager& resources, MaterialTable& materialTable);

private:
    OfflineSceneData data;
    uint64_t previousAssetRevision = ~uint64_t{0};
    std::vector<DrawItem> previousItems;
};
