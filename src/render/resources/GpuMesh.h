#pragma once

#include <slang-rhi.h>
#include "asset/MeshAsset.h"
#include "asset/MeshGeometry.h"

#include <cstdint>
#include <span>


class GpuMesh
{
public:
    GpuMesh(rhi::IDevice& device, rhi::ICommandEncoder& commands, const MeshGeometry& geometry,
        std::span<const MeshAsset::Submesh> submeshes);

    GpuMesh(const GpuMesh&)                = delete;
    GpuMesh& operator=(const GpuMesh&)     = delete;
    GpuMesh(GpuMesh&&) noexcept            = default;
    GpuMesh& operator=(GpuMesh&&) noexcept = default;

    [[nodiscard]] rhi::IBuffer*                          vertices() const { return vertexBuffer; }
    [[nodiscard]] rhi::IBuffer*                          indices() const { return indexBuffer; }
    [[nodiscard]] uint32_t                               vertexCount() const { return vertexTotal; }

private:
    uint32_t                        vertexTotal = 0;
    rhi::ComPtr<rhi::IBuffer>       vertexBuffer;
    rhi::ComPtr<rhi::IBuffer>       indexBuffer;
};
