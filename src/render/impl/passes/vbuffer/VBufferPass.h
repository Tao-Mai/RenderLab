#pragma once

#include "render/graph/RenderPass.h"
#include "render/resources/RayTracingProgram.h"

class VBufferPass final : public RenderPass
{
public:
    static constexpr std::string_view kType = "VBufferPass";

    explicit VBufferPass(rhi::ComPtr<rhi::IDevice> device);

    void execute(RenderContext* context, const RenderData& renderData) override;

private:
    RayTracingProgram program;
};
