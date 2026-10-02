#pragma once

#include "render/RenderConfig.h"
#include "render/ShaderManager.h"
#include "render/scene/GpuScene.h"

#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class PipelineManager;
class VulkanContext;
class RenderGraph;

class ShadowPass
{
public:
    enum Input { Depth, InputCount };
    enum Output { ShadowMap, OutputCount };

    void init(RenderGraph& graph);
    void registerPass(RenderGraph& graph);
    void setupPass(RenderGraph& graph);
    void bindResources(RenderGraph& graph);
    void executePass(RenderGraph& graph) const;

private:
    static constexpr uint32_t mapSize = 1024;

    std::vector<uint32_t> inputSlots;
    std::vector<uint32_t> outputSlots;
    ShaderHandle shader;
    vk::raii::Sampler sampler = nullptr;
    vk::Pipeline pipeline = nullptr;
    vk::PipelineLayout pipelineLayout = nullptr;
};
