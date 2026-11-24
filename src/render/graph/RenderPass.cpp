#include "render/graph/RenderPass.h"

#include "core/Logger.h"

#include <algorithm>
#include <utility>

std::optional<size_t> RenderData::findSlot(std::string_view name) const
{
    const auto found = std::ranges::find(ports, name, &ResourceCache::Ports::value_type::first);
    if (found != ports.end())
        return found->second;
    return std::nullopt;
}

rhi::IResource* RenderData::getResource(std::string_view name) const
{
    const auto slot = findSlot(name);
    return slot ? resources.resource(*slot) : nullptr;
}

rhi::ITexture* RenderData::getTexture(std::string_view name) const
{
    return static_cast<rhi::ITexture*>(getResource(name));
}

rhi::IBuffer* RenderData::getBuffer(std::string_view name) const
{
    return static_cast<rhi::IBuffer*>(getResource(name));
}

uint64_t RenderData::getHistoryRevision(std::string_view name) const
{
    return resources.historyRevision(*findSlot(name));
}

void RenderData::invalidateHistory(std::string_view name) const { resources.invalidateHistory(*findSlot(name)); }

ref<RenderPass> RenderPass::create(
    std::string_view type, rhi::ComPtr<rhi::IDevice> device, const Properties& properties, PluginManager& plugins
)
{
    if (!plugins.hasClass<RenderPass>(type))
        plugins.loadPluginByName(type);

    auto pass = plugins.createClass<RenderPass>(type, std::move(device), properties);
    CHECK(pass, "cannot create render pass '{}'", type);
    return pass;
}

void RenderPass::setProperties(const Properties& values)
{
    CHECK(values.empty(), "pass has no configurable properties");
}
