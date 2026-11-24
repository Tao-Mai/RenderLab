#pragma once

#include "render/graph/RenderPass.h"
#include "render/graph/ResourceCache.h"

#include <string_view>
#include <vector>

class RenderGraphExe
{
public:
    void execute(RenderContext* context);
    void renderUI(const PassEditorContext& context, Gui::Widgets& widget);
    [[nodiscard]] rhi::ComPtr<rhi::IResource> getResource(const std::string& name) const;
    [[nodiscard]] rhi::ComPtr<rhi::IResource> getOutput(size_t index) const
    {
        return rhi::ComPtr<rhi::IResource>{resources.resource(outputs[index])};
    }
    [[nodiscard]] uint32_t getMaxFramesInFlight() const { return maxFramesInFlight; }
    [[nodiscard]] bool hasPassType(std::string_view type) const;
    void setInput(const std::string& name, rhi::IResource* resource);

private:
    friend class RenderGraphCompiler;

    struct Pass
    {
        ref<RenderPass> pass;
        ResourceCache::Ports ports;
    };

    std::vector<Pass> passes;
    std::vector<size_t> outputs;
    ResourceCache resources;
    glm::uvec2 defaultTexDims{0};
    uint32_t maxFramesInFlight = kMaxFramesInFlight;
};
