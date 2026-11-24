#pragma once

#include <slang-rhi.h>
#include "render/RenderConfig.h"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <tuple>
#include <vector>

class GpuMesh;
class RenderResourceManager;
struct PreparedScene;

class SceneAccelerationStructures
{
public:
    // Mesh uploads and the frame slot's previous submission must be complete.
    [[nodiscard]] rhi::IAccelerationStructure* recordBuilds(
        rhi::IDevice&          device,
        rhi::ICommandEncoder&  commands,
        RenderResourceManager& resources,
        const PreparedScene&   renderData,
        uint32_t               frameIndex
    );
    void reset() noexcept;

private:
    struct Frame
    {
        rhi::ComPtr<rhi::IAccelerationStructure> tlas;
        rhi::ComPtr<rhi::IBuffer>                scratch;
        rhi::ComPtr<rhi::IBuffer>                instances;
        uint32_t                                 instanceCount = 0;
    };

    std::map<std::tuple<const GpuMesh*, uint32_t, uint32_t>, rhi::ComPtr<rhi::IAccelerationStructure>> meshBlasCache;
    rhi::ComPtr<rhi::IAccelerationStructure> rectLightBlas;
    std::array<Frame, kMaxFramesInFlight>    frames;

    [[nodiscard]] std::vector<rhi::AccelerationStructureHandle> recordBlasBuilds(
        rhi::IDevice&          device,
        rhi::ICommandEncoder&  commands,
        RenderResourceManager& resources,
        const PreparedScene&   renderData
    );
    void recordRectLightBlasBuild(rhi::IDevice& device, rhi::ICommandEncoder& commands);
    void recordTlasBuild(
        rhi::IDevice&                                                  device,
        rhi::ICommandEncoder&                                          commands,
        Frame&                                                         frame,
        std::span<const rhi::AccelerationStructureInstanceDescGeneric> instances
    );
};
