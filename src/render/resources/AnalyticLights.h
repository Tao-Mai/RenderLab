#pragma once

#include "render/resources/ShaderData.h"
#include "scene/AnalyticLight.h"

#include <slang-rhi.h>
#include <span>
#include <vector>

class AnalyticLights
{
public:
    void init(rhi::IDevice& device);
    bool update(const std::vector<AnalyticLight>& lights,
        rhi::IDevice& device, rhi::ICommandEncoder& commands);

    [[nodiscard]] std::span<const LightData> data() const { return lights; }
    [[nodiscard]] rhi::IBuffer* buffer() const { return lightBuffer; }

private:
    std::vector<AnalyticLight> source;
    std::vector<LightData> lights;
    rhi::ComPtr<rhi::IBuffer> lightBuffer;
};
