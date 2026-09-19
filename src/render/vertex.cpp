#include "render/vertex.h"

#include <cstddef>

vk::VertexInputBindingDescription Vertex::bindingDescription()
{
    return {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = vk::VertexInputRate::eVertex
    };
}

std::array<vk::VertexInputAttributeDescription, 3> Vertex::attributeDescriptions()
{
    return {
        vk::VertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = static_cast<uint32_t>(offsetof(Vertex, position))
        },
        vk::VertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = static_cast<uint32_t>(offsetof(Vertex, normal))
        },
        vk::VertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = static_cast<uint32_t>(offsetof(Vertex, texcoord))
        }
    };
}

std::array<vk::VertexInputAttributeDescription, 2>
Vertex::positionUvAttributeDescriptions()
{
    const auto attributes = attributeDescriptions();
    return {attributes[0], attributes[2]};
}
