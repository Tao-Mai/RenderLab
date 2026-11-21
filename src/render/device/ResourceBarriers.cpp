#include "render/device/ResourceBarriers.h"

#include "core/Logger.h"

#include <algorithm>

void ResourceBarriers::add(VulkanResources::ImageHandle image, VulkanResources::ImageAccess access)
{
    add({image.id, access});
}

void ResourceBarriers::add(VulkanResources::BufferHandle buffer, VulkanResources::MemoryAccess access)
{
    add({buffer.id, {.stages = access.stages, .access = access.access}});
}

void ResourceBarriers::add(VulkanResources::Access use)
{
    const auto found = std::ranges::find(uses, use.slot, &VulkanResources::Access::slot);
    if (found == uses.end())
    {
        uses.push_back(use);
        return;
    }
    DCHECK(found->access.layout == use.access.layout);
    found->access.stages |= use.access.stages;
    found->access.access |= use.access.access;
}

void ResourceBarriers::declare(VulkanResources& cache) const
{
    for (const auto& use : uses)
        cache.declareAccess(use);
}

void ResourceBarriers::transition(VulkanResources& resources, vk::raii::CommandBuffer& commands) const
{
    for (const auto& use : uses)
        resources.transition(commands, use);
}
