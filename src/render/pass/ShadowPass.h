#pragma once

#include "render/RenderConfig.h"
#include "render/ShaderManager.h"

#include <array>
#include <cstdint>
#include <meta>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class PipelineManager;
class VulkanContext;
class RenderGraph;

class ShadowPass
{
public:
    enum Input
    {
        Depth
    };
    enum Output
    {
        ShadowMap
    };

    static constexpr int inputCount  = std::meta::enumerators_of(^^Input).size();
    static constexpr int outputCount = std::meta::enumerators_of(^^Output).size();

    void init(RenderGraph& graph);
    void registerPass(RenderGraph& graph);
    void setupPass(RenderGraph& graph);
    void bindResources(RenderGraph& graph);
    void executePass(RenderGraph& graph) const;

private:
    uint32_t                  pointShadowMapSize = 0;
    static constexpr uint32_t pointViewMask      = 0b111111;

    std::vector<uint32_t> inputSlots;
    std::vector<uint32_t> outputSlots;
    ShaderHandle          shader;
    vk::raii::Sampler     sampler        = nullptr;
    vk::Pipeline          pipeline       = nullptr;
    vk::PipelineLayout    pipelineLayout = nullptr;
};
