#pragma once

#include "render/resource/mesh.h"
#include "ecs/light.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

class RenderResourceManager;

class LightMarkers
{
public:
    void init(RenderResourceManager& resources);
    void reset() noexcept;

    void record(
        vk::raii::CommandBuffer& commandBuffer,
        vk::PipelineLayout pipelineLayout,
        const ecs::Light& light) const;
    void recordPicking(
        vk::raii::CommandBuffer& commandBuffer,
        vk::PipelineLayout pipelineLayout,
        const ecs::Light& light,
        uint32_t selectionId) const;

private:
    struct Part
    {
        Mesh* mesh;
        glm::mat4 model;
        glm::vec3 color;
        uint32_t firstIndex;
        uint32_t indexCount;
    };

    Mesh* sphere = nullptr;
    Mesh* cube = nullptr;
    Mesh* arrow = nullptr;

    [[nodiscard]] std::vector<Part> parts(const ecs::Light& light) const;
};
