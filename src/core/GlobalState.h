#pragma once

#include <slang-rhi.h>
#include "core/Macros.h"


RENDERLAB_CORE_API void          setActivePythonRenderGraphDevice(rhi::ComPtr<rhi::IDevice> device);
[[nodiscard]] RENDERLAB_CORE_API rhi::ComPtr<rhi::IDevice> getActivePythonRenderGraphDevice();
[[nodiscard]] RENDERLAB_CORE_API rhi::ComPtr<rhi::IDevice> accessActivePythonRenderGraphDevice();
