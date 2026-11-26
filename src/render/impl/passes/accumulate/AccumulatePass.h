#pragma once

#include "render/graph/RenderPass.h"

#include <cstdint>

class AccumulatePass final : public RenderPass
{
public:
    static constexpr std::string_view kType = "AccumulatePass";

    explicit AccumulatePass(rhi::ComPtr<rhi::IDevice> device);

    void compile(RenderContext* context, const CompileData& compileData) override;
    void setScene(GpuScene* scene) override;
    void onSceneUpdates(RenderContext* context, SceneUpdateFlags updates) override;
    void execute(RenderContext* context, const RenderData& renderData) override;

private:
    rhi::ComPtr<rhi::IComputePipeline> pipeline;

    uint64_t sampleCount = 0;
};
