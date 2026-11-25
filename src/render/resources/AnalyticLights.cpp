#include "render/resources/AnalyticLights.h"

#include "render/RhiUtils.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <utility>

namespace
{
constexpr uint32_t kMaxAnalyticLights = 65536;
}

void AnalyticLights::init(rhi::IDevice& device)
{
    rhiCheck(device.createBuffer(
        {.size = sizeof(LightData),
         .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination},
        nullptr, lightBuffer.writeRef()
    ));
}

bool AnalyticLights::update(
    const std::vector<AnalyticLight>& next,
    rhi::IDevice& device, rhi::ICommandEncoder& commands
)
{
    if (source == next)
        return false;

    lights.clear();
    for (const auto& light : next)
    {
        if (!light.enabled)
            continue;

        LightData data;
        switch (light.type)
        {
            case AnalyticLightType::Point: data.lightType = GpuLightType::Point; break;
            case AnalyticLightType::Spot: data.lightType = GpuLightType::Spot; break;
            case AnalyticLightType::Directional: data.lightType = GpuLightType::Directional; break;
            case AnalyticLightType::Rectangle: data.lightType = GpuLightType::RectArea; break;
            default: std::unreachable();
        }

        data.positionRange = {light.transform.position, light.range};
        const auto direction = light.transform.rotation * glm::vec3{0, 0, -1};
        data.direction = {glm::normalize(direction), 0.0f};
        data.areaRightData = {glm::normalize(light.transform.rotation * glm::vec3{1, 0, 0}), 0.0f};
        data.areaSizeCone = {light.areaSize, light.cosInner, light.cosOuter};

        float intensity = light.intensity;
        CHECK(std::isfinite(intensity) && intensity >= 0.0f, "invalid intensity for light '{}'", light.name);
        
        if (light.type == AnalyticLightType::Rectangle)
        {
            const float area = light.areaSize.x * light.areaSize.y;
            CHECK(std::isfinite(area) && light.areaSize.x > 0.0f && light.areaSize.y > 0.0f);
            intensity /= area;
        }
        
        data.color = light.color * intensity;

        lights.push_back(data);
    }

    CHECK(lights.size() <= kMaxAnalyticLights, "scene has {} analytic lights; maximum is {}", lights.size(), kMaxAnalyticLights);
    
    if (!lights.empty())
    {
        const auto bytes = lights.size() * sizeof(LightData);
        ensureBufferCapacity(device, lightBuffer, bytes);
        rhiCheck(commands.uploadBufferData(lightBuffer, 0, bytes, lights.data()));
    }
    source = next;
    return true;
}
