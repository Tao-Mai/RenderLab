#pragma once

#include "render/device/ResourceBarriers.h"
#include "asset/EnvironmentMapAsset.h"
#include "core/math/Vector.h"
#include "render/resources/ShaderData.h"

#include <array>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <vector>

struct RestirSettings;
namespace Falcor
{
class RenderContext;
}  // namespace Falcor
class ShaderLayout;

class RestirResources
{
    static constexpr size_t kBindingCount = 5;

public:
    void compile(
        const ShaderLayout& shader,
        const std::array<VulkanResources::BufferHandle, kBindingCount>& buffers,
        ResourceBarriers& accesses
    );
    [[nodiscard]] const auto& layoutBindings() const { return bindings; }
    void prepare(
        VulkanResources& cache, const Falcor::RenderContext& frame, Falcor::uint2 extent, const RestirSettings& settings, vk::DescriptorSet set
    );
    void invalidateHistory() { hasHistory = false; }
    void advanceHistory();

private:
    struct Frame
    {
        std::array<uint64_t, kBindingCount> bufferRevisions{};
        uint32_t surfaceLayer = ~0u;
    };

    std::array<Frame, maxFramesInFlight> frames;
    std::array<vk::DescriptorSetLayoutBinding, kBindingCount> bindings;
    std::array<VulkanResources::BufferHandle, 2>                surfaces;
    VulkanResources::BufferHandle                               initialReservoirs;
    VulkanResources::BufferHandle                               reservoirHistory;
    VulkanResources::BufferHandle                               parameters;
    uint32_t writeLayer = 0;
    bool hasHistory = false;
    glm::mat4 previousViewProjection{1.0f};
    glm::vec4 previousViewport{};
    glm::vec4 previousDepthRow{};
    uint64_t previousSceneSignature = 0;
    uint64_t previousMaterialRevision = 0;
    bool previousIbl = false;
    std::vector<LightData> previousLights;
    std::optional<EnvironmentMapAsset::ID> previousEnvironment;

    void updateDescriptors(VulkanResources& cache, const Falcor::RenderContext& frame, vk::DescriptorSet set);
};
