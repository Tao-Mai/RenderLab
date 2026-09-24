#pragma once

#include "asset/Vertex.h"

#include <array>
#include <cstddef>

#include <vulkan/vulkan.hpp>

namespace VertexLayout
{
[[nodiscard]] inline vk::VertexInputBindingDescription bindingDescription()
{
    return {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = vk::VertexInputRate::eVertex,
    };
}

[[nodiscard]] inline std::array<vk::VertexInputAttributeDescription, 3>
attributeDescriptions()
{
    return {
        vk::VertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = static_cast<uint32_t>(offsetof(Vertex, position)),
        },
        vk::VertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = static_cast<uint32_t>(offsetof(Vertex, normal)),
        },
        vk::VertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = static_cast<uint32_t>(offsetof(Vertex, texcoord)),
        },
    };
}

[[nodiscard]] inline std::array<vk::VertexInputAttributeDescription, 1>
positionAttributeDescription()
{
    return {
        vk::VertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = static_cast<uint32_t>(offsetof(Vertex, position)),
        },
    };
}
}
