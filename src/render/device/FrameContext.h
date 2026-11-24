#pragma once
#include <slang-rhi.h>
#include "render/device/GpuProfiler.h"

// Resources whose lifetime follows one in-flight frame slot.
struct FrameContext
{
    void                              init(rhi::IDevice& device, rhi::ICommandQueue& queue);
    void                              reset() noexcept;
    void                              wait();
    void                              begin();
    void                              submit();
    [[nodiscard]] GpuProfiler&        gpuProfiler() { return profiling; }
    rhi::ComPtr<rhi::ICommandEncoder> commands;

private:
    rhi::ComPtr<rhi::IDevice>        device;
    rhi::ComPtr<rhi::ICommandQueue>  queue;
    rhi::ComPtr<rhi::IFence>         fence;
    rhi::ComPtr<rhi::ICommandBuffer> submitted;
    uint64_t                         fenceValue = 0;
    GpuProfiler                      profiling;
};
