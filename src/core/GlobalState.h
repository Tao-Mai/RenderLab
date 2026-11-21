#pragma once

#include "core/Macros.h"
#include "core/Object.h"

namespace Falcor
{
class Device;

FALCOR_API void                      setActivePythonRenderGraphDevice(ref<Device> device);
[[nodiscard]] FALCOR_API ref<Device> getActivePythonRenderGraphDevice();
[[nodiscard]] FALCOR_API ref<Device> accessActivePythonRenderGraphDevice();
}  // namespace Falcor
