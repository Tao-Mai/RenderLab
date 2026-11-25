#pragma once

#include <slang-rhi.h>
#include "core/Config.h"

#include <memory>

class RenderGraph;

[[nodiscard]] std::shared_ptr<RenderGraph> buildMinimalPathTracerGraph(
    rhi::ComPtr<rhi::IDevice> device, const RenderGraphSettings& settings
);
