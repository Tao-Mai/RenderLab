#pragma once

#include "render/graph/RenderPass.h"
#include "render/graph/ResourceCache.h"

#include <memory>
#include <string_view>
#include <vector>

class RenderGraphExe
{
public:
    void execute(RenderContext* context, RenderData::Dictionary& dictionary, glm::uvec2 defaultTexDims);
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
        std::shared_ptr<RenderPass> pass;
        ResourceCache::Ports        ports;
    };

    std::vector<Pass> passes;
    std::vector<size_t> outputs;
    ResourceCache resources;
    uint32_t maxFramesInFlight = kMaxFramesInFlight;
};
