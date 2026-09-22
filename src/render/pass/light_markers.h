#pragma once

#include "render/resource/mesh.h"
#include "scene/light.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

class AssetManager;

class LightMarkers
{
public:
    void initialize(GpuUploadContext upload, AssetManager& assets);
    void reset() noexcept;

    void record(
        vk::raii::CommandBuffer& commandBuffer,
        vk::PipelineLayout pipelineLayout,
        const Light& light) const;
    void recordPicking(
        vk::raii::CommandBuffer& commandBuffer,
        vk::PipelineLayout pipelineLayout,
        const Light& light,
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

    std::unique_ptr<Mesh> sphere;
    std::unique_ptr<Mesh> cube;
    std::unique_ptr<Mesh> arrow;

    [[nodiscard]] std::vector<Part> parts(const Light& light) const;
};
