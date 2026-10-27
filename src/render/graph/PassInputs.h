#pragma once

#include <cstdint>
#include <span>
#include <vulkan/vulkan.hpp>

class VulkanContext;
class FrameData;
class GpuProfiler;
struct RenderView;
struct RenderData;
struct RendererConfig;
struct EditorFrameInput;

// Borrowed inputs supplied by Renderer, not stored or exposed by RenderGraph.
struct PassSetup
{
    VulkanContext&        vulkan;
    vk::Extent2D          renderExtent;
    std::span<FrameData>  frameData;
    const RendererConfig& config;
};

struct PassFrame
{
    const PassSetup&        setup;
    FrameData&              data;
    const RenderView&       view;
    const RenderData&       renderData;
    const EditorFrameInput& editor;
    uint32_t                frameIndex;
    GpuProfiler*            gpuProfiler = nullptr;
};
