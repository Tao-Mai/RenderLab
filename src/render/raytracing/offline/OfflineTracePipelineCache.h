#pragma once

#include "render/raytracing/RayTracingPipeline.h"

#include <future>
#include <map>
#include <optional>
#include <string>
#include <string_view>

class VulkanContext;

class OfflineTracePipelineCache
{
public:
    [[nodiscard]] const RayTracingPipeline* get(
        const VulkanContext& vulkan, vk::PipelineLayout layout, std::string_view shader, bool background
    );
    void clear() noexcept;

private:
    struct Build
    {
        std::optional<RayTracingPipeline> result;
        std::future<RayTracingPipeline> future;
    };

    std::map<std::pair<vk::PipelineLayout, std::string>, Build> builds;
};
