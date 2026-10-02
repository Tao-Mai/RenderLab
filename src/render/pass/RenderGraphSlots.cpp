#include "render/pass/RenderGraph.h"

#include "core/Logger.h"

#include <algorithm>
#include <format>
#include <functional>
#include <utility>

namespace
{
constexpr auto writeAccess = vk::AccessFlagBits2::eShaderWrite |
    vk::AccessFlagBits2::eShaderStorageWrite |
    vk::AccessFlagBits2::eColorAttachmentWrite |
    vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
    vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eHostWrite |
    vk::AccessFlagBits2::eMemoryWrite;
}

std::tuple<std::vector<RenderGraph::ResourceHandle>, std::vector<RenderGraph::ResourceHandle>>
RenderGraph::registerPass(std::string_view name, uint32_t inputSlotCount,
    uint32_t outputSlotCount, std::function<void(RenderGraph&)> setup,
    std::function<void(RenderGraph&)> execute)
{
    CHECK(!name.empty() && !passInfos.contains(std::string(name)),
        "duplicate or empty render pass name '{}'", name);

    auto info = std::make_unique<PassInfo>();
    info->name = name;
    info->setupPass = std::move(setup);
    info->executePass = std::move(execute);
    for (uint32_t i = 0; i < inputSlotCount; ++i)
    {
        info->inputResourceHandles.push_back(static_cast<ResourceHandle>(globalInputSlots.size()));
        globalInputSlots.push_back({.owner = info.get()});
    }
    for (uint32_t i = 0; i < outputSlotCount; ++i)
    {
        info->outputResourceHandles.push_back(static_cast<ResourceHandle>(globalOutputSlots.size()));
        globalOutputSlots.push_back({.owner = info.get()});
    }

    auto handles = std::make_tuple(info->inputResourceHandles, info->outputResourceHandles);
    registeredPasses.push_back(info.get());
    passInfos.emplace(std::string(name), std::move(info));
    compiled = false;
    built = false;
    return handles;
}

void RenderGraph::setPassEnabled(std::string_view name, bool enabled)
{
    const auto found = passInfos.find(std::string(name));
    CHECK(found != passInfos.end(), "unknown render pass '{}'", name);
    found->second->enabled = enabled;
    compiled = false;
    built = false;
}

bool RenderGraph::passEnabled(std::string_view name) const
{
    const auto found = passInfos.find(std::string(name));
    CHECK(found != passInfos.end(), "unknown render pass '{}'", name);
    return found->second->enabled;
}

RenderGraph::ResourceHandle RenderGraph::outputSlot(std::string_view name, uint32_t index) const
{
    const auto found = passInfos.find(std::string(name));
    CHECK(found != passInfos.end() && index < found->second->outputResourceHandles.size(),
        "unknown output {} of render pass '{}'", index, name);
    return found->second->outputResourceHandles[index];
}

void RenderGraph::createResource(ResourceHandle input, const ResourceDesc& desc)
{
    Slot& slot = globalInputSlots.at(input);
    CHECK(slot.binding == Binding::Unbound, "input slot {} is already bound", input);
    slot.binding = Binding::Resource;
    slot.resource = static_cast<ResourceHandle>(resources.size());
    slot.desc = desc;
    resources.push_back({.creation = desc, .merged = desc});
    compiled = false;
    built = false;
}

RenderGraph::ResourceHandle RenderGraph::importExternal(const ResourceDesc& desc,
    std::vector<ExternalResource> instances, bool swapchainIndexed)
{
    CHECK(!instances.empty(), "external resources require physical instances");
    for (ResourceHandle index = 0; index < resources.size(); ++index)
    {
        const Resource& resource = resources[index];
        if (resource.external && resource.creation.type == desc.type &&
            resource.swapchainIndexed == swapchainIndexed &&
            resource.instances.size() == instances.size() &&
            std::equal(instances.begin(), instances.end(), resource.instances.begin(),
                [](const ExternalResource& a, const ExternalResource& b)
                { return a.image == b.image && a.buffer == b.buffer; }))
        {
            CHECK(resource.creation.format == desc.format && resource.creation.extent == desc.extent &&
                resource.creation.size == desc.size && resource.creation.mipLevels == desc.mipLevels &&
                resource.creation.arrayLayers == desc.arrayLayers && resource.creation.aspect == desc.aspect &&
                resource.creation.imageUsage == desc.imageUsage && resource.creation.bufferUsage == desc.bufferUsage,
                "inconsistent descriptions of an external render resource");
            return index;
        }
    }

    const auto index = static_cast<ResourceHandle>(resources.size());
    resources.push_back({.creation = desc, .merged = desc, .external = true,
        .swapchainIndexed = swapchainIndexed, .instances = std::move(instances)});
    return index;
}

void RenderGraph::importResource(ResourceHandle input, const ResourceDesc& desc,
    std::vector<ExternalResource> instances, bool swapchainIndexed)
{
    Slot& slot = globalInputSlots.at(input);
    CHECK(slot.binding == Binding::Unbound, "external input slot {} is already bound", input);
    slot.binding = Binding::Resource;
    slot.resource = importExternal(desc, std::move(instances), swapchainIndexed);
    slot.desc = desc;
    compiled = false;
    built = false;
}

void RenderGraph::bindInput(ResourceHandle input, ResourceHandle output, const ResourceDesc& desc)
{
    Slot& slot = globalInputSlots.at(input);
    CHECK(slot.binding == Binding::Unbound, "input slot {} is already bound", input);
    CHECK(slot.owner != globalOutputSlots.at(output).owner,
        "a pass input must depend on another pass output; create new resources on its input");
    slot.binding = Binding::Alias;
    slot.ref = output;
    slot.desc = desc;
    compiled = false;
    built = false;
}

void RenderGraph::bindOutput(ResourceHandle output, ResourceHandle input, const ResourceDesc& desc)
{
    Slot& slot = globalOutputSlots.at(output);
    CHECK(slot.binding == Binding::Unbound && slot.owner == globalInputSlots.at(input).owner,
        "output slot {} must bind an unbound output to its own pass input", output);
    slot.binding = Binding::Alias;
    slot.ref = input;
    slot.desc = desc;
    compiled = false;
    built = false;
}

void RenderGraph::ignoreInput(ResourceHandle input)
{
    Slot& slot = globalInputSlots.at(input);
    CHECK(slot.binding == Binding::Unbound, "input slot {} is already bound", input);
    slot.binding = Binding::Ignored;
    compiled = false;
    built = false;
}

std::vector<std::string> RenderGraph::compile()
{
    compiled = false;
    built = false;
    orderedPasses.clear();
    std::vector<std::string> errors;
    std::vector<uint8_t> inputVisits(globalInputSlots.size());
    std::vector<uint8_t> outputVisits(globalOutputSlots.size());
    for (Resource& resource : resources)
    {
        resource.merged = resource.creation;
    }

    std::function<bool(bool, ResourceHandle)> resolve = [&](bool isInput, ResourceHandle index)
    {
        Slot& slot = isInput ? globalInputSlots.at(index) : globalOutputSlots.at(index);
        auto& visits = isInput ? inputVisits : outputVisits;
        if (!slot.owner->enabled)
        {
            errors.push_back(std::format("dependency on disabled pass '{}'", slot.owner->name));
            return false;
        }
        if (visits[index] == 1)
        {
            errors.push_back(std::format("resource reference cycle at '{}'", slot.owner->name));
            return false;
        }
        if (visits[index] >= 2) return visits[index] == 2;

        visits[index] = 1;
        bool valid = true;
        if (slot.binding == Binding::Unbound)
        {
            errors.push_back(std::format("unbound {} slot {} of '{}'",
                isInput ? "input" : "output", index, slot.owner->name));
            slot.resource = invalidHandle;
            valid = false;
        }
        else if (slot.binding == Binding::Ignored)
        {
            slot.resource = invalidHandle;
        }
        else if (slot.binding == Binding::Alias)
        {
            valid = resolve(!isInput, slot.ref);
            const Slot& source = isInput ? globalOutputSlots[slot.ref] : globalInputSlots[slot.ref];
            if (!valid || source.binding == Binding::Ignored)
            {
                errors.push_back(std::format("{} slot {} of '{}' has no resource",
                    isInput ? "input" : "output", index, slot.owner->name));
                slot.resource = invalidHandle;
                valid = false;
            }
            else
            {
                slot.resource = source.resource;
            }
        }
        visits[index] = valid ? 2 : 3;
        return valid;
    };

    std::unordered_map<PassInfo*, size_t> passIndices;
    for (size_t i = 0; i < registeredPasses.size(); ++i)
    {
        passIndices.emplace(registeredPasses[i], i);
        if (!registeredPasses[i]->enabled)
        {
            continue;
        }
        for (ResourceHandle input : registeredPasses[i]->inputResourceHandles)
        {
            (void)resolve(true, input);
        }
        for (ResourceHandle output : registeredPasses[i]->outputResourceHandles)
        {
            (void)resolve(false, output);
        }
    }

    const size_t count = registeredPasses.size();
    std::vector<std::vector<bool>> edges(count, std::vector<bool>(count));
    for (const Slot& slot : globalInputSlots)
    {
        if (slot.owner->enabled && slot.binding == Binding::Alias)
        {
            edges[passIndices.at(globalOutputSlots[slot.ref].owner)][passIndices.at(slot.owner)] = true;
        }
    }
    auto reachable = edges;
    for (size_t k = 0; k < count; ++k)
    {
        for (size_t i = 0; i < count; ++i)
        {
            for (size_t j = 0; j < count; ++j)
                reachable[i][j] = reachable[i][j] || (reachable[i][k] && reachable[k][j]);
        }
    }

    std::vector<std::vector<bool>> touched(count, std::vector<bool>(resources.size()));
    std::vector<std::vector<bool>> writes = touched;
    const auto mergeUse = [&](const Slot& slot, ResourceHandle handle)
    {
        Resource& resource = resources.at(handle);
        const size_t passIndex = passIndices.at(slot.owner);
        touched[passIndex][handle] = true;
        writes[passIndex][handle] = writes[passIndex][handle] ||
            static_cast<bool>(slot.desc.access & writeAccess);
        if (resource.creation.type != slot.desc.type)
        {
            errors.push_back(std::format("resource type mismatch in '{}'", slot.owner->name));
        }
        if (slot.desc.format != vk::Format::eUndefined && slot.desc.format != resource.creation.format)
        {
            errors.push_back(std::format("resource format mismatch in '{}'", slot.owner->name));
        }
        if (slot.desc.extent.width != 0 && slot.desc.extent != resource.creation.extent)
        {
            errors.push_back(std::format("resource extent mismatch in '{}'", slot.owner->name));
        }
        if (slot.desc.size != 0 && slot.desc.size != resource.creation.size)
        {
            errors.push_back(std::format("resource buffer size mismatch in '{}'", slot.owner->name));
        }
        if (slot.desc.type == ResourceType::Image && slot.desc.layout == vk::ImageLayout::eUndefined)
        {
            errors.push_back(std::format("undefined image use layout in '{}'", slot.owner->name));
        }
        if (slot.desc.access && !slot.desc.stages)
        {
            errors.push_back(std::format("resource access without pipeline stage in '{}'", slot.owner->name));
        }
        resource.merged.imageUsage |= slot.desc.imageUsage;
        resource.merged.bufferUsage |= slot.desc.bufferUsage;
    };
    const auto mergeSlot = [&](const Slot& slot)
    {
        if (slot.owner->enabled && slot.resource != invalidHandle)
            mergeUse(slot, slot.resource);
    };
    for (const Slot& slot : globalInputSlots) mergeSlot(slot);
    for (const Slot& slot : globalOutputSlots) mergeSlot(slot);

    // Simultaneous input uses must agree. A pass's intra-pass state transitions
    // are represented by output states and applied with useOutput instead.
    for (const PassInfo* pass : registeredPasses)
    {
        if (!pass->enabled) continue;
        for (size_t a = 0; a < pass->inputResourceHandles.size(); ++a)
        {
            const Slot& left = globalInputSlots[pass->inputResourceHandles[a]];
            for (size_t b = a + 1; b < pass->inputResourceHandles.size(); ++b)
            {
                const Slot& right = globalInputSlots[pass->inputResourceHandles[b]];
                const bool overlaps = left.resource != invalidHandle && left.resource == right.resource;
                if (overlaps && (left.desc.layout != right.desc.layout ||
                     static_cast<bool>((left.desc.access | right.desc.access) & writeAccess)))
                {
                    errors.push_back(std::format("conflicting input uses of one resource in '{}'", pass->name));
                }
            }
        }
    }

    for (size_t r = 0; r < resources.size(); ++r)
    {
        const Resource& resource = resources[r];
        const ResourceDesc& desc = resource.merged;
        if (desc.type == ResourceType::Image &&
            (desc.format == vk::Format::eUndefined || desc.extent.width == 0 ||
             desc.extent.height == 0 || desc.extent.depth == 0 || desc.mipLevels == 0 ||
             desc.arrayLayers == 0 || !desc.aspect || !desc.imageUsage))
        {
            errors.push_back(std::format("incomplete image creation description for resource {}", r));
        }
        if (desc.type == ResourceType::Buffer && (desc.size == 0 || !desc.bufferUsage))
        {
            errors.push_back(std::format("incomplete buffer creation description for resource {}", r));
        }
        if (desc.type == ResourceType::Image && desc.viewType == vk::ImageViewType::eCube &&
            (!(desc.imageFlags & vk::ImageCreateFlagBits::eCubeCompatible) || desc.arrayLayers != 6 ||
             desc.extent.width != desc.extent.height || desc.extent.depth != 1))
        {
            errors.push_back(std::format("invalid cubemap creation description for resource {}", r));
        }
        if (resource.external &&
            ((resource.creation.imageUsage & desc.imageUsage) != desc.imageUsage ||
             (resource.creation.bufferUsage & desc.bufferUsage) != desc.bufferUsage))
        {
            errors.push_back(std::format("external resource {} lacks required usage flags", r));
        }

        for (size_t a = 0; a < count; ++a)
        {
            for (size_t b = a + 1; b < count; ++b)
            {
                if (touched[a][r] && touched[b][r] && (writes[a][r] || writes[b][r]) &&
                    !reachable[a][b] && !reachable[b][a])
                {
                    errors.push_back(std::format("unordered shared-resource hazard between '{}' and '{}'",
                        registeredPasses[a]->name, registeredPasses[b]->name));
                }
            }
        }
    }

    std::vector<bool> emitted(count);
    for (size_t iteration = 0; iteration < count; ++iteration)
    {
        bool progress = false;
        for (size_t i = 0; i < count; ++i)
        {
            if (emitted[i] || !registeredPasses[i]->enabled) continue;
            bool ready = true;
            for (size_t j = 0; j < count; ++j)
                if (edges[j][i] && !emitted[j]) ready = false;
            if (ready)
            {
                emitted[i] = true;
                orderedPasses.push_back(registeredPasses[i]);
                progress = true;
            }
        }
        if (!progress) break;
    }
    for (size_t i = 0; i < count; ++i)
        if (registeredPasses[i]->enabled && !emitted[i])
            errors.push_back(std::format("pass dependency cycle or disabled producer blocks '{}'",
                registeredPasses[i]->name));

    compiled = errors.empty();
    if (!compiled) orderedPasses.clear();
    return errors;
}

std::vector<std::string> RenderGraph::executionOrder() const
{
    std::vector<std::string> names;
    for (const PassInfo* pass : orderedPasses) names.push_back(pass->name);
    return names;
}

const RenderGraph::ResourceDesc& RenderGraph::resourceDesc(ResourceHandle input) const
{
    CHECK(compiled, "render graph must be compiled before resolving resources");
    return resources.at(globalInputSlots.at(input).resource).merged;
}

bool RenderGraph::needsBarrier(const ResourceState& previous, const ResourceDesc& next)
{
    return (next.type == ResourceType::Image && previous.layout != next.layout) ||
        static_cast<bool>((previous.access | next.access) & writeAccess);
}
