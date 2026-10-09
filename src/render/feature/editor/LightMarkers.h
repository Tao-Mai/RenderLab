#pragma once

#include "scene/component/LightComponent.h"

#include <cstdint>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

class GpuMesh;
class RenderResourceManager;
class AActor;
struct SceneAsset;
struct TransformComponent;

class LightMarkers
{
public:
    struct DrawItem
    {
        GpuMesh*  mesh;
        glm::mat4 model;
        glm::vec3 color;
        uint32_t  firstIndex;
        uint32_t  indexCount;
        uint32_t  selectionId = 0;
    };

    void load(const SceneAsset& scene, RenderResourceManager& resources);
    void prepare();
    void reset() noexcept;

    [[nodiscard]] std::span<const DrawItem> drawItems() const { return draws; }

    void record(
        vk::raii::CommandBuffer& commandBuffer, vk::PipelineLayout pipelineLayout, std::span<const DrawItem> items
    ) const;
    void recordPicking(
        vk::raii::CommandBuffer& commandBuffer, vk::PipelineLayout pipelineLayout, std::span<const DrawItem> items
    ) const;

private:
    std::vector<AActor*>  actors;
    std::vector<DrawItem> draws;

    [[nodiscard]] std::vector<DrawItem>
    buildDrawItems(const TransformComponent& transform, const LightComponent& light, uint32_t selectionId) const;

    GpuMesh* sphere = nullptr;
    GpuMesh* cube   = nullptr;
    GpuMesh* arrow  = nullptr;
};
