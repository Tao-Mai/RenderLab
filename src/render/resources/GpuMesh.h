#pragma once

#include <slang-rhi.h>
#include "asset/MeshAsset.h"
#include "asset/MeshGeometry.h"

#include <cstdint>
#include <vector>


class GpuMesh
{
public:
    GpuMesh(rhi::IDevice& device, MeshGeometry geometry, std::vector<MeshAsset::Submesh> submeshes, bool rayTracing);

    GpuMesh(const GpuMesh&)                = delete;
    GpuMesh& operator=(const GpuMesh&)     = delete;
    GpuMesh(GpuMesh&&) noexcept            = default;
    GpuMesh& operator=(GpuMesh&&) noexcept = default;

    [[nodiscard]] uint32_t                               indexCount() const { return indexTotal; }
    [[nodiscard]] const std::vector<MeshAsset::Submesh>& submeshes() const { return parts; }
    [[nodiscard]] rhi::IBuffer*                          vertices() const { return vertexBuffer; }
    [[nodiscard]] rhi::IBuffer*                          indices() const { return indexBuffer; }
    [[nodiscard]] uint32_t                               vertexCount() const { return vertexTotal; }

private:
    uint32_t                        indexTotal  = 0;
    uint32_t                        vertexTotal = 0;
    std::vector<MeshAsset::Submesh> parts;
    rhi::ComPtr<rhi::IBuffer>       vertexBuffer;
    rhi::ComPtr<rhi::IBuffer>       indexBuffer;
};
