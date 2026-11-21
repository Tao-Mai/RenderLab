#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "render/graph/RenderGraph.h"
#include "render/resources/ShaderData.h"

#include <array>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <vector>

struct RestirSettings;
class ShaderLayout;

class RestirResources
{
public:
    [[nodiscard]] RenderGraph::BufferHandle compile(RenderGraph& graph, const PassSetup& setup,
                                                    const ShaderLayout& shader);
    [[nodiscard]] const auto& layoutBindings() const { return bindings; }
    void prepare(RenderGraph& graph, const PassFrame& frame, const RestirSettings& settings, vk::DescriptorSet set);
    void invalidateHistory() { hasHistory = false; }
    void advanceHistory();

private:
    static constexpr size_t kBindingCount = 5;

    struct Frame
    {
        std::array<uint64_t, kBindingCount> bufferRevisions{};
        uint32_t surfaceLayer = ~0u;
    };

    std::array<Frame, maxFramesInFlight> frames;
    std::array<vk::DescriptorSetLayoutBinding, kBindingCount> bindings;
    std::array<RenderGraph::BufferHandle, 2> surfaces;
    RenderGraph::BufferHandle initialReservoirs;
    RenderGraph::BufferHandle reservoirHistory;
    RenderGraph::BufferHandle parameters;
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

    void updateDescriptors(RenderGraph& graph, const PassFrame& frame, vk::DescriptorSet set);
};
