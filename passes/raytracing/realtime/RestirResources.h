#pragma once
#include <slang-rhi.h>

#include "asset/EnvironmentMapAsset.h"
#include "core/math/Vector.h"
#include "render/device/GpuResources.h"
#include "render/resources/ShaderData.h"

#include <array>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <vector>

struct RestirSettings;
namespace Falcor
{
struct RenderContext;
}  // namespace Falcor
class ShaderLayout;

class RestirResources
{
    static constexpr size_t kBindingCount = 4;

public:
    void compile(const std::array<GpuResources::BufferHandle, kBindingCount>& buffers);
    void prepare(
        GpuResources&                cache,
        const Falcor::RenderContext& frame,
        Falcor::uint2                extent,
        const RestirSettings&        settings,
        rhi::IShaderObject*          object
    );
    void invalidateHistory() { hasHistory = false; }
    void advanceHistory();

private:
    std::array<GpuResources::BufferHandle, 2> surfaces;
    GpuResources::BufferHandle                initialReservoirs;
    GpuResources::BufferHandle                reservoirHistory;
    uint32_t                                  writeLayer = 0;
    bool                                      hasHistory = false;
    glm::mat4                                 previousViewProjection{1.0f};
    glm::vec4                                 previousViewport{};
    glm::vec4                                 previousDepthRow{};
    uint64_t                                  previousSceneSignature   = 0;
    uint64_t                                  previousMaterialRevision = 0;
    bool                                      previousIbl              = false;
    std::vector<LightData>                    previousLights;
    std::optional<EnvironmentMapAsset::ID>    previousEnvironment;
};
