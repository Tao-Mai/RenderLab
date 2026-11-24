#include "core/GlobalState.h"

#include "core/Logger.h"

#include <utility>

namespace
{
    thread_local rhi::ComPtr<rhi::IDevice> spActivePythonRenderGraphDevice;
}

void setActivePythonRenderGraphDevice(rhi::ComPtr<rhi::IDevice> device)
{
    spActivePythonRenderGraphDevice = std::move(device);
}

rhi::ComPtr<rhi::IDevice> getActivePythonRenderGraphDevice() { return spActivePythonRenderGraphDevice; }

rhi::ComPtr<rhi::IDevice> accessActivePythonRenderGraphDevice()
{
    CHECK(spActivePythonRenderGraphDevice, "render graph scripting requires an active device");
    return spActivePythonRenderGraphDevice;
}
