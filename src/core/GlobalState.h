#pragma once

#include <slang-rhi.h>
#include "core/Macros.h"

namespace Falcor
{

FALCOR_API void          setActivePythonRenderGraphDevice(rhi::ComPtr<rhi::IDevice> device);
[[nodiscard]] FALCOR_API rhi::ComPtr<rhi::IDevice> getActivePythonRenderGraphDevice();
[[nodiscard]] FALCOR_API rhi::ComPtr<rhi::IDevice> accessActivePythonRenderGraphDevice();
}  // namespace Falcor
