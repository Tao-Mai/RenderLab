#pragma once

#include "render/raytracing/RealtimeScene.h"
#include "render/resources/ShaderLayout.h"

#include <array>
#include <span>

class RealtimeSceneBindings
{
public:
    void use(RenderGraph& graph, const ShaderLayout& shader, RealtimeScene::Buffers geometry,
             RenderGraph::BufferHandle lights);
    void appendLayoutBindings(std::vector<vk::DescriptorSetLayoutBinding>& bindings) const;
    [[nodiscard]] static vk::PipelineLayout getPipelineLayout(
        Device& device, const ShaderLayout& shader, const DescriptorSetLayout& passLayout,
        std::span<const vk::PushConstantRange> ranges
    );
    void prepare(RenderGraph& graph, const PassFrame& frame, vk::DescriptorSet passSet);
    void bind(vk::raii::CommandBuffer& commands, const PassFrame& frame, vk::PipelineLayout layout,
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

    void updateTlas(const PassFrame& frame, vk::DescriptorSet passSet);
};
