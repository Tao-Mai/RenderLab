#pragma once

#include <slang-rhi.h>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

class GpuMesh;
struct GpuDraw;

class SceneAccelerationStructures
{
public:
    static constexpr uint32_t kTriangleAttributeSize = 2 * sizeof(float);

    void invalidate() { ++revision; }

    // Record on the scene's graphics queue. Reuse the TLAS until its geometry changes.
    [[nodiscard]] rhi::IAccelerationStructure* getOrBuild(
        rhi::IDevice& device,
        rhi::ICommandEncoder& commands,
        std::span<const GpuDraw> draws
    );

private:
    std::map<std::tuple<const GpuMesh*, uint32_t, uint32_t>, rhi::ComPtr<rhi::IAccelerationStructure>> meshBlasCache;
    rhi::ComPtr<rhi::IAccelerationStructure> tlas;
    uint64_t                                 revision = 0;
    std::optional<uint64_t>                   builtRevision;

    [[nodiscard]] std::vector<rhi::AccelerationStructureHandle> recordBlasBuilds(
        rhi::IDevice& device, rhi::ICommandEncoder& commands, std::span<const GpuDraw> draws
    );
    [[nodiscard]] rhi::ComPtr<rhi::IAccelerationStructure> recordTlasBuild(
        rhi::IDevice&                                                  device,
        rhi::ICommandEncoder&                                          commands,
        std::span<const rhi::AccelerationStructureInstanceDescGeneric> instances
    );
};
