#pragma once

#include "render/graph/RenderPass.h"

#include "render/graph/RenderGraph.h"

class EditorUiPass : public RenderPass
{
public:

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) override;

private:
    RenderGraph::ImageHandle color;
};
