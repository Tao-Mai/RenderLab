#pragma once

#include "render/graph/RenderPass.h"
#include "render/resources/RayTracingProgram.h"

#include <optional>

class RestirPathTracer final : public RenderPass
{
public:
    static constexpr std::string_view kType = "RestirPathTracer";

    explicit RestirPathTracer(rhi::ComPtr<rhi::IDevice> device);
    RenderPassReflection reflect(const ResourceCache::DefaultProperties& defaults) override;
    void compile(RenderContext* context, const CompileData& compileData) override;
    void setScene(GpuScene* scene) override;
    void onSceneUpdates(RenderContext* context, SceneUpdateFlags updates) override;
    void execute(RenderContext* context, const RenderData& renderData) override;

private:
    std::optional<RayTracingProgram> tracer;
    uint32_t sampleSeed = 0;
};
