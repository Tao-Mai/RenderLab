#pragma once

#include "asset/Asset.h"
#include "render/device/Device.h"
#include "render/graph/RenderGraph.h"

#include <array>

class ExposurePass
{
public:
    struct State
    {
        float    ev100;
        float    exposureScale;
        uint32_t automaticValid;
        uint32_t padding;
    };
    static_assert(sizeof(State) == 16);

    struct Output
    {
        RenderGraph::BufferHandle state;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle hdrColor);
    void   writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame);

private:
    RenderGraph::ImageHandle          source;
    RenderGraph::BufferHandle         histogram;
    Output                            output;
    DescriptorSetLayout               descriptorSetLayout;
    vk::PipelineLayout                pipelineLayout;
    std::array<vk::raii::Pipeline, 2> pipelines{nullptr, nullptr};
    vk::raii::DescriptorSet           descriptorSet = nullptr;
    vk::Extent3D                      extent;
    AssetID<struct SceneAsset>        previousScene;
    bool                              hasHistory = false;
};
