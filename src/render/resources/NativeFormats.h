#pragma once

#include "render/resources/Formats.h"

#include <vulkan/vulkan.hpp>

namespace Falcor
{
[[nodiscard]] vk::Format     getVulkanFormat(ResourceFormat format);
[[nodiscard]] ResourceFormat getResourceFormat(vk::Format format);
}  // namespace Falcor
