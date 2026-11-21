#pragma once

#include "render/device/VulkanResources.h"

// A group of accesses issued before one set of commands inside a render pass.
class ResourceBarriers
{
public:
    void clear() { uses.clear(); }
    [[nodiscard]] bool empty() const { return uses.empty(); }
    void add(VulkanResources::ImageHandle image, VulkanResources::ImageAccess access);
    void add(VulkanResources::BufferHandle buffer, VulkanResources::MemoryAccess access);
    void declare(VulkanResources& cache) const;
    void transition(VulkanResources& resources, vk::raii::CommandBuffer& commands) const;

private:
    std::vector<VulkanResources::Access> uses;
    void                               add(VulkanResources::Access use);
};

namespace vulkan_access
{
inline constexpr auto kDepthStages =
    vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
inline constexpr VulkanResources::ImageAccess kColorWrite{
    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    vk::AccessFlagBits2::eColorAttachmentWrite,
    vk::ImageLayout::eColorAttachmentOptimal
};
inline constexpr VulkanResources::ImageAccess kColorReadWrite{
    kColorWrite.stages, kColorWrite.access | vk::AccessFlagBits2::eColorAttachmentRead, kColorWrite.layout
};
inline constexpr VulkanResources::ImageAccess kDepthRead{
    kDepthStages, vk::AccessFlagBits2::eDepthStencilAttachmentRead, vk::ImageLayout::eDepthReadOnlyOptimal
};
inline constexpr VulkanResources::ImageAccess kDepthWrite{
    kDepthStages,
    kDepthRead.access | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
    vk::ImageLayout::eDepthAttachmentOptimal
};
inline constexpr VulkanResources::ImageAccess kTransferSource{
    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead, vk::ImageLayout::eTransferSrcOptimal
};
inline constexpr VulkanResources::ImageAccess kTransferTarget{
    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eTransferDstOptimal
};
inline constexpr VulkanResources::MemoryAccess kTransferWrite{
    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite
};
inline constexpr VulkanResources::ImageAccess kPresent{.layout = vk::ImageLayout::ePresentSrcKHR};
inline constexpr VulkanResources::MemoryAccess kHostRead{
    vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead
};

constexpr VulkanResources::ImageAccess sampled(vk::PipelineStageFlags2 stages)
{
    return {stages, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eShaderReadOnlyOptimal};
}

constexpr VulkanResources::MemoryAccess storageRead(vk::PipelineStageFlags2 stages)
{
    return {stages, vk::AccessFlagBits2::eShaderStorageRead};
}
}  // namespace vulkan_access
