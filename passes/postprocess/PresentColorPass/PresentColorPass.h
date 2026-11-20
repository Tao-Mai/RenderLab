#pragma once

#include "render/graph/RenderPass.h"

#include "render/graph/RenderGraph.h"

class PresentColorPass : public CompositeRenderPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;

private:
    class BlitPass
    {
    public:
        Output compile(
            RenderGraph& graph, const PassSetup& setup,
            RenderGraph::ImageHandle sourceHandle, RenderGraph::ImageHandle targetHandle
        );
        void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame&) const;

    private:
        RenderGraph::ImageHandle source;
        RenderGraph::ImageHandle target;
    };

    BlitPass blit;
};
