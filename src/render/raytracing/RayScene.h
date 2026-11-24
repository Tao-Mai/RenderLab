#pragma once

#include "render/device/GpuResources.h"

#include <optional>

struct RenderContext;

struct PreparedScene;
class RenderResourceManager;

class RayScene
{
public:
    void                        setup(GpuResources& cache, rhi::IDevice& device);
    void                        upload(const PreparedScene& scene, RenderResourceManager& resources);
    void                        bind(rhi::IShaderObject* object, const RenderContext& frame) const;
    [[nodiscard]] rhi::IBuffer* getTriangleBuffer() const { return cache->getBuffer(triangles); }

private:
    GpuResources*              cache = nullptr;
    GpuResources::BufferHandle objects, triangles, areaEmissions, environmentLight;
    std::optional<uint64_t>    uploadedRevision;
};
