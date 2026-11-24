#include "render/device/FrameContext.h"
#include "core/Context.h"
#include "render/device/RhiCheck.h"

void FrameContext::init(rhi::IDevice& targetDevice, rhi::ICommandQueue& targetQueue)
{
    device = &targetDevice;
    queue  = &targetQueue;
    rhiCheck(device->createFence({}, fence.writeRef()));
    profiling.init(targetDevice);
}
void FrameContext::reset() noexcept
{
    profiling.reset();
    commands.setNull();
    submitted.setNull();
    fence.setNull();
    device     = nullptr;
    queue      = nullptr;
    fenceValue = 0;
}
void FrameContext::wait()
{
    auto* handle = fence.get();
    rhiCheck(device->waitForFences(1, &handle, &fenceValue, true, rhi::kTimeoutInfinite));
    profiling.collect(context().profiler);
    submitted.setNull();
}
void FrameContext::begin() { rhiCheck(queue->createCommandEncoder(commands.writeRef())); }
void FrameContext::submit()
{
    rhiCheck(commands->finish(submitted.writeRef()));
    commands.setNull();
    auto* buffer = submitted.get();
    auto* signal = fence.get();
    ++fenceValue;
    rhi::SubmitDesc desc;
    desc.commandBuffers     = &buffer;
    desc.commandBufferCount = 1;
    desc.signalFences       = &signal;
    desc.signalFenceValues  = &fenceValue;
    desc.signalFenceCount   = 1;
    rhiCheck(queue->submit(desc));
}
