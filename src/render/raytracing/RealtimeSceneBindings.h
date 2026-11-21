#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/raytracing/RealtimeScene.h"
#include "render/resources/ShaderLayout.h"

#include <array>
#include <span>

class RealtimeSceneBindings
{
public:
    void compile(ResourceBarriers& accesses, const ShaderLayout& shader, RealtimeScene::Buffers geometry);
    void appendLayoutBindings(std::vector<vk::DescriptorSetLayoutBinding>& bindings) const;
    [[nodiscard]] static vk::PipelineLayout getPipelineLayout(
        Falcor::Device& device, const ShaderLayout& shader, const DescriptorSetLayout& passLayout,
        std::span<const vk::PushConstantRange> ranges
    );
    void prepare(VulkanResources& cache, const Falcor::RenderContext& frame, vk::DescriptorSet passSet);
    void bind(vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame, vk::PipelineLayout layout,
              vk::DescriptorSet passSet);

private:
    struct Frame
    {
        std::array<uint64_t, 2> bufferRevisions{};
        uint64_t tlasRevision = 0;
    };

    RealtimeScene::Buffers buffers;
    std::array<Frame, maxFramesInFlight> frames;
    ShaderBinding<vk::DescriptorType::eAccelerationStructureKHR> tlasBinding;
    ShaderBinding<vk::DescriptorType::eStorageBuffer> objectsBinding;
    ShaderBinding<vk::DescriptorType::eStorageBuffer> trianglesBinding;

    void updateTlas(const Falcor::RenderContext& frame, vk::DescriptorSet passSet);
};
