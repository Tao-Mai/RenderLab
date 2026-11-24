#include "render/graph/RenderPassReflection.h"

#include <algorithm>
#include <utility>

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
