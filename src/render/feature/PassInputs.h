#pragma once

#include <cstdint>
#include <span>

class VulkanContext;
class Swapchain;
class FrameData;
struct RenderView;
struct RenderData;
struct EditorFrameInput;

// Borrowed inputs supplied by Renderer, not stored or exposed by RenderGraph.
struct PassSetup
{
    VulkanContext&       vulkan;
    Swapchain&           swapchain;
    std::span<FrameData> frameData;
};

struct PassFrame
{
    const PassSetup&        setup;
    FrameData&              data;
    const RenderView&       view;
    const RenderData&       renderData;
    const EditorFrameInput& editor;
    uint32_t                frameIndex;
};
