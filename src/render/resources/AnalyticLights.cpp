#include "render/resources/AnalyticLights.h"

#include "render/RhiUtils.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
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
            case AnalyticLightType::Point: data.type = LightType::Point; break;
            case AnalyticLightType::Directional: data.type = LightType::Directional; break;
            case AnalyticLightType::Rectangle: data.type = LightType::Rect; break;
            default: std::unreachable();
        }

        CHECK(std::isfinite(light.openingAngle) && light.openingAngle >= 0.0f &&
                  light.openingAngle <= glm::pi<float>(), "invalid opening angle for light '{}'", light.name);
        CHECK(std::isfinite(light.penumbraAngle) && light.penumbraAngle >= 0.0f &&
                  light.penumbraAngle <= light.openingAngle, "invalid penumbra angle for light '{}'", light.name);

        data.posW = light.transform.position;
        const auto direction = light.transform.rotation * glm::vec3{0, 0, -1};
        data.dirW = glm::normalize(direction);
        data.openingAngle = light.openingAngle;
        data.cosOpeningAngle = std::cos(light.openingAngle);
        data.penumbraAngle = light.penumbraAngle;

        float intensity = light.intensity;
        CHECK(std::isfinite(intensity) && intensity >= 0.0f, "invalid intensity for light '{}'", light.name);

        if (light.type == AnalyticLightType::Rectangle)
        {
            const float area = light.areaSize.x * light.areaSize.y;
            CHECK(std::isfinite(area) && light.areaSize.x > 0.0f && light.areaSize.y > 0.0f);

            const glm::vec3 right = glm::normalize(light.transform.rotation * glm::vec3{1, 0, 0});
            const glm::vec3 up = glm::cross(right, data.dirW);
            data.surfaceArea = area;
            data.transMat = glm::mat4{
                glm::vec4{right * (0.5f * light.areaSize.x), 0.0f},
                glm::vec4{up * (0.5f * light.areaSize.y), 0.0f},
                glm::vec4{data.dirW, 0.0f},
                glm::vec4{data.posW, 1.0f},
            };
            data.transMatIT = glm::transpose(glm::inverse(data.transMat));
            intensity /= area;
        }

        data.intensity = light.color * intensity;

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
