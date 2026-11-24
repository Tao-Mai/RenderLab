#include "render/resources/AnalyticLights.h"

#include "asset/pbrt/PbrtSpectrum.h"
#include "render/device/BufferCapacity.h"

#include <array>
#include <cmath>
#include <concepts>

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
    const std::vector<SceneAnalyticLight>& next, const AssetDataManager& assets,
    rhi::IDevice& device, rhi::ICommandEncoder& commands
)
{
    if (source == next)
        return false;

    std::array<uint32_t, static_cast<size_t>(GpuLightType::Spot) + 1> shadowCounts{};
    lights.clear();
    for (const auto& light : next)
    {
        const auto append = [&](LightData data)
        {
            data.color = pbrtPreviewRgb(light.emission.spectrum, assets) * static_cast<float>(light.emission.scale);
            data.shadowEnabled = light.castShadow ? 1u : 0u;
            if (light.castShadow)
                data.shadowIdx = shadowCounts[static_cast<size_t>(data.type())]++;
            lights.push_back(data);
        };
        std::visit([&]<class T>(const T& model)
        {
            if constexpr (std::same_as<T, SceneAnalyticLight::Point>)
                append({.positionRange = {model.position, model.range}, .lightType = GpuLightType::Point});
            else if constexpr (std::same_as<T, SceneAnalyticLight::Directional>)
                append({.direction = {model.direction, 0}, .lightType = GpuLightType::Directional});
            else if constexpr (std::same_as<T, SceneAnalyticLight::Spot>)
                append({
                    .spotFalloffExponent = model.falloffExponent,
                    .positionRange = {model.position, model.range},
                    .direction = {model.direction, 0},
                    .areaSizeCone = {0, 0, model.cosInner, model.cosOuter},
                    .lightType = GpuLightType::Spot,
                    .areaRightData = {model.right, 0},
                });
            else if constexpr (std::same_as<T, SceneAnalyticLight::Rectangle>)
            {
                CHECK(std::isfinite(model.size.x) && std::isfinite(model.size.y) && model.size.x > 0 && model.size.y > 0,
                    "rectangular light requires finite positive dimensions");
                append({
                    .positionRange = {model.position, model.range},
                    .direction = {model.direction, 0},
                    .areaSizeCone = {model.size, 0, 0},
                    .lightType = GpuLightType::RectArea,
                    .areaRightData = {model.right, 0},
                });
            }
        }, light.model);
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
