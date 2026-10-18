#include "render/feature/RenderGraph.h"

#include "core/Logger.h"

#include <algorithm>
#include <queue>
#include <unordered_map>

namespace
{
constexpr auto kWriteAccess = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderStorageWrite |
    vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
    vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eHostWrite | vk::AccessFlagBits2::eMemoryWrite |
    vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
}

RenderGraph::ImageHandle RenderGraph::createImage(ImageCreateInfo info)
{
    DCHECK(info.format != vk::Format::eUndefined && info.extent.width && info.extent.height);
    const auto id = static_cast<ResourceHandle>(resources.size());
    resources.push_back({.data = std::make_unique<ResourceData>(ResourceData{.imageInfo = info})});
    return {id};
}

RenderGraph::BufferHandle RenderGraph::createBuffer(BufferCreateInfo info)
{
    DCHECK(info.size);
    const auto id = static_cast<ResourceHandle>(resources.size());
    resources.push_back(
        {.data = std::make_unique<ResourceData>(ResourceData{.type = ResourceType::Buffer, .bufferInfo = info})}
    );
    return {id};
}

RenderGraph::AccelerationStructureHandle RenderGraph::createAccelerationStructure(MemoryAccess initial)
{
    const auto id = static_cast<ResourceHandle>(resources.size());
    resources.push_back(
        {.data = std::make_unique<ResourceData>(ResourceData{
             .type         = ResourceType::AccelerationStructure,
             .initialState = {.stages = initial.stages, .access = initial.access},
         })}
    );
    return {id};
}

RenderGraph::ImageHandle RenderGraph::write(ImageHandle image)
{
    DCHECK(setupNode);
    DCHECK(resolveResource(image.id).type == ResourceType::Image);

    const auto id = static_cast<ResourceHandle>(resources.size());
    resources.push_back({.previous = image.id, .owner = setupNode});
    setupNode->writeSrc.push_back(image.id);

    return {id};
}

RenderGraph::BufferHandle RenderGraph::write(BufferHandle buffer)
{
    DCHECK(setupNode);
    DCHECK(resolveResource(buffer.id).type == ResourceType::Buffer);

    const auto id = static_cast<ResourceHandle>(resources.size());
    resources.push_back({.previous = buffer.id, .owner = setupNode});
    setupNode->writeSrc.push_back(buffer.id);
    return {id};
}

RenderGraph::AccelerationStructureHandle RenderGraph::write(AccelerationStructureHandle structure)
{
    DCHECK(setupNode);
    DCHECK(resolveResource(structure.id).type == ResourceType::AccelerationStructure);

    const auto id = static_cast<ResourceHandle>(resources.size());
    resources.push_back({.previous = structure.id, .owner = setupNode});
    setupNode->writeSrc.push_back(structure.id);
    return {id};
}

RenderGraph::ResourceData& RenderGraph::resolveResource(ResourceHandle handle)
{
    DCHECK(handle >= 0 && handle < resources.size());
    auto* resource = &resources.at(handle);
    while (resource->previous != kInvalidHandle)
        resource = &resources.at(resource->previous);

    DCHECK(resource->data);
    return *resource->data;
}

void RenderGraph::use(ImageHandle image, ImageAccess access)
{
    DCHECK(setupNode && image.id >= 0 && image.id < resources.size());
    auto& resource = resolveResource(image.id);
    DCHECK(resource.type == ResourceType::Image && access.layout != vk::ImageLayout::eUndefined);

    // Creation usage is the union of the actual uses declared by passes.
    if (access.access & (vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite))
        resource.imageUsage |= vk::ImageUsageFlagBits::eColorAttachment;
    if (access.access &
        (vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite))
        resource.imageUsage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (access.access & vk::AccessFlagBits2::eShaderSampledRead)
        resource.imageUsage |= vk::ImageUsageFlagBits::eSampled;
    if (access.access & (vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite))
        resource.imageUsage |= vk::ImageUsageFlagBits::eStorage;
    if (access.access & vk::AccessFlagBits2::eTransferRead)
        resource.imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
    if (access.access & vk::AccessFlagBits2::eTransferWrite)
        resource.imageUsage |= vk::ImageUsageFlagBits::eTransferDst;

    auto&      uses  = setupNode->uses;
    const auto found = std::ranges::find(uses, image.id, &ResourceUse::resource);
    if (found != uses.end())
    {
        DCHECK(found->access.layout == access.layout);
        found->access.stages |= access.stages;
        found->access.access |= access.access;
    }
    else
        uses.push_back({image.id, access});
}

void RenderGraph::use(BufferHandle buffer, MemoryAccess access)
{
    DCHECK(setupNode && buffer.id >= 0 && buffer.id < resources.size());
    auto& resource = resolveResource(buffer.id);
    DCHECK(resource.type == ResourceType::Buffer);

    if (access.access & vk::AccessFlagBits2::eTransferRead)
        resource.bufferUsage |= vk::BufferUsageFlagBits::eTransferSrc;
    if (access.access & vk::AccessFlagBits2::eTransferWrite)
        resource.bufferUsage |= vk::BufferUsageFlagBits::eTransferDst;
    if (access.access & (vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite))
        resource.bufferUsage |= vk::BufferUsageFlagBits::eStorageBuffer;
    if (access.access & vk::AccessFlagBits2::eUniformRead)
        resource.bufferUsage |= vk::BufferUsageFlagBits::eUniformBuffer;

    auto&      uses  = setupNode->uses;
    const auto found = std::ranges::find(uses, buffer.id, &ResourceUse::resource);
    if (found != uses.end())
    {
        found->access.stages |= access.stages;
        found->access.access |= access.access;
    }
    else
        uses.push_back({buffer.id, {.stages = access.stages, .access = access.access}});
}

void RenderGraph::use(AccelerationStructureHandle structure, MemoryAccess access)
{
    DCHECK(setupNode);
    DCHECK(resolveResource(structure.id).type == ResourceType::AccelerationStructure);
    auto&      uses  = setupNode->uses;
    const auto found = std::ranges::find(uses, structure.id, &ResourceUse::resource);
    if (found != uses.end())
    {
        found->access.stages |= access.stages;
        found->access.access |= access.access;
    }
    else
        uses.push_back({structure.id, {.stages = access.stages, .access = access.access}});
}

bool RenderGraph::needsBarrier(const ImageAccess& previous, const ImageAccess& next)
{
    return previous.layout != next.layout || static_cast<bool>((previous.access | next.access) & kWriteAccess);
}

void RenderGraph::compileNodes()
{
    DCHECK(!setupNode);
    CHECK(!nodes.empty());
    sortedNodes.clear();

    std::vector<bool> written(resources.size());
    for (auto* node : nodes)
    {
        node->prev.clear();
        node->next.clear();

        for (ResourceHandle source : node->writeSrc)
        {
            CHECK(source >= 0 && source < resources.size());
            CHECK(!written[source]);
            written[source] = true;
        }
    }

    const auto connect = [](PassNode* producer, PassNode* consumer)
    {
        if (!producer || producer == consumer)
            return;
        if (std::ranges::find(producer->next, consumer) != producer->next.end())
            return;

        producer->next.push_back(consumer);
        consumer->prev.push_back(producer);
    };

    // Follow resource producers backwards from the endpoint; unused nodes remain disconnected.
    std::vector<std::vector<PassNode*>>   consumers(resources.size());
    std::unordered_map<PassNode*, size_t> remaining;
    std::queue<PassNode*>                 pending;
    auto*                                 end = nodes.back();
    remaining.emplace(end, 0);
    pending.push(end);

    while (!pending.empty())
    {
        auto* node = pending.front();
        pending.pop();

        for (ResourceHandle handle : node->dependency)
        {
            CHECK(handle >= 0 && handle < resources.size());
            consumers[handle].push_back(node);
            auto* producer = resources[handle].owner;
            connect(producer, node);
            if (producer && remaining.emplace(producer, 0).second)
                pending.push(producer);
        }
    }

    // Preserve reads of an old version before overwriting it, only between retained nodes.
    for (auto* node : nodes)
    {
        if (!remaining.contains(node))
            continue;

        for (ResourceHandle source : node->writeSrc)
        {
            for (auto* consumer : consumers[source])
                connect(consumer, node);
        }
    }

    // Kahn's algorithm keeps the graph connections intact; isolated non-end nodes are culled.
    for (auto* node : nodes)
    {
        if (node != end && node->next.empty())
            CHECK(node->prev.empty());

        remaining[node] = node->prev.size();
        if (node->prev.empty() && (node == end || !node->next.empty()))
            pending.push(node);
    }

    while (!pending.empty())
    {
        auto* node = pending.front();
        pending.pop();
        sortedNodes.push_back(node);
        remaining.erase(node);

        for (auto* next : node->next)
        {
            auto& count = remaining.at(next);
            CHECK(count);
            if (--count == 0)
                pending.push(next);
        }
    }

    // Any connected node left unsorted belongs to, or is blocked by, a cycle.
    for (const auto& [node, count] : remaining)
        CHECK(node->prev.empty() && node->next.empty());

    // Seed swapchain synchronization from its first retained use.
    for (auto& entry : resources)
        if (entry.data && entry.data->swapchainIndexed)
            entry.data->initialState.stages = {};

    for (auto* node : sortedNodes)
    {
        for (const auto& use : node->uses)
        {
            auto& resource = resolveResource(use.resource);
            if (resource.swapchainIndexed && !resource.initialState.stages)
                resource.initialState.stages = use.access.stages;
        }
    }
}

const RenderGraph::ResourceData& RenderGraph::resolveResource(ResourceHandle handle) const
{
    DCHECK(handle >= 0 && handle < resources.size());
    auto* resource = &resources.at(handle);
    while (resource->previous != kInvalidHandle)
        resource = &resources.at(resource->previous);

    DCHECK(resource->data);
    return *resource->data;
}
