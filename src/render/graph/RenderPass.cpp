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

rhi::TextureDesc RenderPass::CompileData::getTextureDesc(std::string_view name) const
{
    const auto port = std::ranges::find(ports, name, &ResourceCache::Ports::value_type::first);
    return std::get<rhi::TextureDesc>(resources.description(port->second));
}

void RenderPass::CompileData::addInternalTexture(std::string name, const rhi::TextureDesc& desc) const
{
    const auto slot = resources.addResource(passName + '.' + name, desc);
    ports.emplace_back(std::move(name), slot);
}

std::shared_ptr<RenderPass> RenderPass::create(
    std::string_view type, rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties, PluginManager& plugins
)
{
    if (!plugins.hasClass<RenderPass>(type))
        plugins.loadPluginByName(type);

    auto pass = plugins.createClass<RenderPass>(type, std::move(device), properties);
    CHECK(pass, "cannot create render pass '{}'", type);
    return pass;
}

RenderPassReflection RenderPass::reflect(const ResourceCache::DefaultProperties&)
{
    return RenderPassReflection(getPluginInfo().ports);
}

nlohmann::json RenderPass::getProperties() const
{
    const auto get = getPluginInfo().settings.get;
    return get ? get(*this) : nlohmann::json::object();
}

void RenderPass::setProperties(const nlohmann::json& properties)
{
    if (const auto set = getPluginInfo().settings.set)
        set(*this, properties);
    else
        CHECK(properties.empty(), "pass has no configurable properties");
}
