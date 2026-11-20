#pragma once

#include "render/resources/Buffer.h"

#include <future>
#include <map>
#include <optional>
#include <string>
#include <string_view>

class VulkanContext;

struct OfflineTracePipeline
{
    vk::raii::Pipeline                trace = nullptr;
    Buffer                            bindingTable;
    vk::StridedDeviceAddressRegionKHR raygen, miss, hit;
};

class OfflineTracePipelineCache
{
public:
    [[nodiscard]] const OfflineTracePipeline* get(
        const VulkanContext& vulkan, vk::PipelineLayout layout, std::string_view shader, bool background
    );
    void clear() noexcept;

private:
    struct Build
    {
        std::optional<OfflineTracePipeline> result;
        std::future<OfflineTracePipeline> future;
    };

    std::map<std::pair<vk::PipelineLayout, std::string>, Build> builds;
};
