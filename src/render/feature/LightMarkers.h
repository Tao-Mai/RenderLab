#pragma once

#include "render/resource/GpuMesh.h"
#include "scene/component/LightComponent.h"

#include <cstdint>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

class RenderResourceManager;
struct TransformComponent;

class LightMarkers
{
public:
    struct DrawItem
    {
        GpuMesh* mesh;
        glm::mat4 model;
        glm::vec3 color;
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t selectionId = 0;
    };

    void init(RenderResourceManager& resources);
    void reset() noexcept;

    void record(
        vk::raii::CommandBuffer& commandBuffer,
        vk::PipelineLayout pipelineLayout,
        std::span<const DrawItem> draws) const;
    void recordPicking(
        vk::raii::CommandBuffer& commandBuffer,
        vk::PipelineLayout pipelineLayout,
        std::span<const DrawItem> draws) const;

    [[nodiscard]] std::vector<DrawItem> buildDrawItems(
        const TransformComponent& transform, const LightComponent& light,
        uint32_t selectionId) const;

private:
    GpuMesh* sphere = nullptr;
    GpuMesh* cube = nullptr;
    GpuMesh* arrow = nullptr;
};
