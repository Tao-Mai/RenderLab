#include "render/graph/RenderPass.h"

#include "core/Logger.h"
#include "render/plugins/PluginManager.h"

#include <algorithm>

namespace
{
void addPort(
    std::vector<RenderPassPort>& ports, std::string_view name, PortKind kind, bool optional, vk::Format format
)
{
    DCHECK(!name.empty() && name.find('.') == std::string_view::npos);
    DCHECK(std::ranges::find(ports, name, &RenderPassPort::name) == ports.end());
    DCHECK(kind == PortKind::Image || format == vk::Format::eUndefined);
    ports.push_back({std::string{name}, kind, optional, format});
}
}

RenderPassReflection& RenderPassReflection::input(
    std::string_view name, PortKind kind, bool optional, vk::Format format
)
{
    addPort(inputPorts, name, kind, optional, format);
    return *this;
}

RenderPassReflection& RenderPassReflection::output(
    std::string_view name, PortKind kind, bool optional, vk::Format format
)
{
    addPort(outputPorts, name, kind, optional, format);
    return *this;
}

RenderGraph::ImageHandle PassResources::image(std::string_view name) const
{
    const auto found = inputResources.find(name);
    DCHECK(found != inputResources.end());
    return std::get<RenderGraph::ImageHandle>(found->second);
}

RenderGraph::BufferHandle PassResources::buffer(std::string_view name) const
{
    const auto found = inputResources.find(name);
    DCHECK(found != inputResources.end());
    return std::get<RenderGraph::BufferHandle>(found->second);
}

void PassResources::setOutput(std::string_view name, GraphResource resource)
{
    DCHECK(!outputResources.contains(name));
    outputResources.emplace(name, resource);
}

RenderPass::~RenderPass() = default;

std::shared_ptr<RenderPass> RenderPass::create(const PluginManager& plugins, std::string_view type)
{
    return plugins.createPass(type);
}

nlohmann::json RenderPass::properties() const
{
    return nlohmann::json::object();
}

bool RenderPass::setProperties(const nlohmann::json& values)
{
    CHECK(values.is_object() && values.empty(), "pass has no configurable properties");
    return false;
}

nlohmann::json RenderPass::statistics() const
{
    return nlohmann::json::object();
}
