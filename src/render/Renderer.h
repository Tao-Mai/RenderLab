#pragma once

#include "core/Debug.h"

#include "render/DescriptorManager.h"
#include "render/PipelineManager.h"
#include "render/RenderConfig.h"
#include "render/RenderView.h"
#include "render/ShaderManager.h"
#include "render/device/FrameContext.h"
#include "render/device/VulkanContext.h"
#include "render/feature/RenderGraph.h"
#include "render/feature/editor/EditorFrameInput.h"
#include "render/present/Swapchain.h"
#include "render/resource/RenderResourceManager.h"

#include <array>
#include <cstdint>
#include <memory>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

struct RenderData;

class Renderer
{
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void init();
    void              rebuildGraph();
    void              recreateSwapchain();
    RenderFrameResult render(const RenderView& view, const RenderData& renderData, const EditorFrameInput& editor);
    void waitIdle();
    void shutdown() noexcept;

    [[nodiscard]] VulkanContext& vulkanContext() { return vulkan; }
    [[nodiscard]] Swapchain&     swapchainHandle() { return swapchain; }

private:
    struct Passes;
    DEBUG_ONLY(bool inited = false;)
    VulkanContext                             vulkan;
    Swapchain                                 swapchain;
    ShaderManager                             shaders;
    DescriptorManager                         descriptors;
    PipelineManager                           pipelines;
    RenderResourceManager                     resources;
    std::array<FrameContext, maxFramesInFlight> frames;
    uint32_t                                  frameIndex = 0;
    RenderGraph                               graph;
    std::unique_ptr<Passes>                     passes;
    RenderGraph::BufferHandle                  pickingReadback;
    void setupRenderGraph();

    template <class T, class... Args>
    auto setupPass(Args&&... args);

    [[nodiscard]] FrameContext& currentFrame() { return frames[frameIndex]; }
};
