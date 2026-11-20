#pragma once

#include "render/denoise/DenoiserInputs.h"
#include "render/device/Device.h"
#include "render/raytracing/RealtimeScene.h"

#include <array>
#include <span>
#include <vector>

class RealtimePathTracing
{
public:
    struct Program
    {
        std::array<const char*, 2>                       shaderPaths;
        std::span<const char* const>                     raygenEntries;
        std::span<const vk::DescriptorSetLayoutBinding> extraBindings;
    };

    struct Output : DenoiserInputs
    {
        RenderGraph::ImageHandle rayImage;
    };

    [[nodiscard]] Output compile(
        RenderGraph& graph,
        const PassSetup& setup,
        RealtimeScene::Buffers geometry,
        RenderGraph::BufferHandle lights,
        const Program& program
    );
    void bindResources(RenderGraph& graph, const PassSetup& setup);
    void prepare(RenderGraph& graph, const PassFrame& execution);
    void invalidateDenoiserHistory(RenderGraph& graph) const;
    void bind(vk::raii::CommandBuffer& commands, const PassFrame& execution, uint32_t maxBounces);
    void dispatch(vk::raii::CommandBuffer& commands, uint32_t raygenIndex) const;

    [[nodiscard]] vk::DescriptorSet descriptorSet(uint32_t frameIndex) const { return *frames[frameIndex].set; }

private:
    struct Frame
    {
        std::array<uint64_t, 2> bufferRevisions{};
        vk::raii::DescriptorSet set = nullptr;
        uint64_t tlasRevision = 0;
    };

    struct Pipeline
    {
        DescriptorSetLayout descriptorSetLayout;
        vk::PipelineLayout layout;
        vk::raii::Pipeline pipeline = nullptr;
        Buffer bindingTable;
        std::vector<vk::StridedDeviceAddressRegionKHR> raygenRegions;
        vk::StridedDeviceAddressRegionKHR missRegion, hitRegion;
    };

    Output output;
    RealtimeScene::Buffers geometry;
    std::array<Pipeline, 2> pipelines;
    std::array<Frame, maxFramesInFlight> frames;
    vk::Extent2D extent;
    uint32_t sampleIndex = 0;
    bool svgfEnabled = false;

    void createPipeline(const PassSetup& setup, Pipeline& pipeline, const Program& program);
    void updateTlasDescriptor(const PassFrame& execution, Frame& frame);
};
