#pragma once

#include <slang-rhi.h>

#include <span>

struct GpuDraw;
class MaterialTable;
class ShaderManager;

namespace rhi
{
struct ShaderCursor;
}

class EmissiveLights
{
public:
    void invalidate() { dirty = true; }
    void prepare(rhi::IDevice& device, rhi::ICommandEncoder& commands, ShaderManager& shaders,
        std::span<const GpuDraw> draws, const MaterialTable& materials);

    [[nodiscard]] uint32_t count() const { return triangleCount; }
    void bind(const rhi::ShaderCursor& cursor) const;

private:
    rhi::ComPtr<rhi::IComputePipeline> buildPipeline;
    rhi::ComPtr<rhi::IBuffer> meshBuffer;
    rhi::ComPtr<rhi::IBuffer> instanceOffsets;
    rhi::ComPtr<rhi::IBuffer> triangles;
    uint32_t triangleCount = 0;
    bool dirty = true;
};
