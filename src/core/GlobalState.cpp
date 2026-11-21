#include "core/GlobalState.h"

#include "core/Logger.h"

#include <utility>

namespace Falcor
{
namespace
{
thread_local ref<Device> spActivePythonRenderGraphDevice;
}

void setActivePythonRenderGraphDevice(ref<Device> device)
{
    spActivePythonRenderGraphDevice = std::move(device);
}

ref<Device> getActivePythonRenderGraphDevice() { return spActivePythonRenderGraphDevice; }

ref<Device> accessActivePythonRenderGraphDevice()
{
    CHECK(spActivePythonRenderGraphDevice, "render graph scripting requires an active device");
    return spActivePythonRenderGraphDevice;
}
}  // namespace Falcor
