#pragma once
#include <slang-rhi.h>

#include <glm/vec2.hpp>

#include <array>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

struct RestirSettings;
class GpuScene;

class RestirResources
{
    static constexpr size_t kBindingCount = 4;

public:
    void               compile(const std::array<rhi::IBuffer*, kBindingCount>& buffers);
    void prepare(const GpuScene& scene, glm::uvec2 extent, const RestirSettings& settings, rhi::IShaderObject* object);
    void invalidateHistory() { hasHistory = false; }
    void advanceHistory();

private:
    std::array<rhi::ComPtr<rhi::IBuffer>, 2>  surfaces;
    rhi::ComPtr<rhi::IBuffer>                 initialReservoirs;
    rhi::ComPtr<rhi::IBuffer>                 reservoirHistory;
    uint32_t                                  writeLayer = 0;
    bool                                      hasHistory = false;
    glm::mat4                                 previousViewProjection{1.0f};
    glm::vec4                                 previousDepthRow{};
};
