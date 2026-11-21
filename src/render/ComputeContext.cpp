#include "render/ComputeContext.h"

#include "core/Logger.h"
#include "render/device/ResourceBarriers.h"
#include "render/resources/Buffer.h"
#include "render/resources/Texture.h"

#include <array>
#include <bit>

namespace Falcor
{
namespace
{
template <typename T>
void clearUAV(CopyContext& context, const UnorderedAccessView* view, const std::array<T, 4>& value)
{
    DCHECK(view && view->getResource());
    auto* resource = view->getResource();
    auto& commands = *context.commands;
    auto& resources = *context.vulkanResources;
    const auto& range = view->getViewInfo();

    if (resource->getType() == Resource::Type::Buffer)
    {
        const auto buffer = resource->asBuffer();
        DCHECK(buffer);
        const auto size = range.size == ResourceViewInfo::kEntireBuffer ? buffer->getSize() - range.offset : range.size;
        CHECK(buffer->usage() & vk::BufferUsageFlagBits::eTransferDst, "buffer requires transfer destination usage");
        CHECK(
            range.offset % sizeof(uint32_t) == 0 && size % sizeof(uint32_t) == 0,
            "buffer UAV clear requires a 4-byte aligned range (offset {}, size {})",
            range.offset,
            size
        );
        const auto word = std::bit_cast<uint32_t>(value[0]);
        DCHECK(word == std::bit_cast<uint32_t>(value[1]) && word == std::bit_cast<uint32_t>(value[2]) &&
               word == std::bit_cast<uint32_t>(value[3]));

        resources.transition(commands, resources.registerBuffer(buffer), vulkan_access::kTransferWrite);
        commands.fillBuffer(buffer->handle(), range.offset, size, word);
        return;
    }

    const auto texture = resource->asTexture();
    DCHECK(texture);
    resources.transition(commands, resources.registerImage(texture), vulkan_access::kTransferTarget);

    const uint32_t faces = texture->getType() == Resource::Type::TextureCube ? 6u : 1u;
    const vk::ImageSubresourceRange subresources{
        vk::ImageAspectFlagBits::eColor,
        range.mostDetailedMip,
        range.mipCount,
        range.firstArraySlice * faces,
        range.arraySize * faces,
    };
    commands.clearColorImage(texture->handle(), vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue{value}, subresources);
}
}  // namespace

void ComputeContext::clearUAV(const UnorderedAccessView* view, const float4& value)
{
    Falcor::clearUAV(*this, view, std::array{value.x, value.y, value.z, value.w});
}

void ComputeContext::clearUAV(const UnorderedAccessView* view, const uint4& value)
{
    Falcor::clearUAV(*this, view, std::array{value.x, value.y, value.z, value.w});
}
}  // namespace Falcor
