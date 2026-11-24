#include "render/graph/RenderPassReflection.h"

#include <algorithm>
#include <utility>

RenderPassReflection::RenderPassReflection(std::span<const Field> fields) : ports(fields.begin(), fields.end()) {}

void RenderPassReflection::addField(Field field) { ports.push_back(std::move(field)); }

void RenderPassReflection::applyGraphTextureProperties(rhi::Extent3D extent, rhi::Format format)
{
    for (auto& port : ports)
    {
        if (!port.matchInput.empty() || port.textureMatch == TextureMatch::None)
            continue;

        auto& texture = std::get<rhi::TextureDesc>(port.resource);
        if (is_set(port.textureMatch, TextureMatch::Extent))
            texture.size = extent;
        if (is_set(port.textureMatch, TextureMatch::Format))
            texture.format = format;
    }
}

void RenderPassReflection::addInput(std::string name, ResourceDesc resource, bool optional)
{
    ports.push_back({std::move(name), std::move(resource), Visibility::Input, optional});
}

void RenderPassReflection::addOutput(std::string name, ResourceDesc resource, bool optional)
{
    ports.push_back({std::move(name), std::move(resource), Visibility::Output, optional});
}

void RenderPassReflection::addInputOutput(std::string name, ResourceDesc resource)
{
    ports.push_back({std::move(name), std::move(resource), Visibility::Input | Visibility::Output});
}

void RenderPassReflection::addInternal(std::string name, ResourceDesc resource)
{
    ports.push_back({std::move(name), std::move(resource), Visibility::Internal});
}

const RenderPassReflection::Field* RenderPassReflection::getField(std::string_view name) const
{
    const auto found = std::ranges::find(ports, name, &Field::name);
    return found != ports.end() ? &*found : nullptr;
}
