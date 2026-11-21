#pragma once

#include "core/Metadata.h"
#include "render/graph/RenderPass.h"

#include <type_traits>

namespace pass_ports
{
template <class T, class Visit>
void fields(T& value, Visit&& visit)
{
    metadata::visitFields(value, [&](const auto& field, auto& handle)
    {
        visit(std::string_view{field.name}, handle);
    });
}

template <class Handle>
constexpr PortKind kind()
{
    static_assert(std::is_same_v<Handle, RenderGraph::ImageHandle> || std::is_same_v<Handle, RenderGraph::BufferHandle>);
    return std::is_same_v<Handle, RenderGraph::ImageHandle> ? PortKind::Image : PortKind::Buffer;
}

template <class T>
void inputs(RenderPassReflection& reflection)
{
    T resources{};
    fields(resources, [&]<class Handle>(std::string_view name, Handle&) { reflection.input(name, kind<Handle>()); });
}

template <class T>
void outputs(RenderPassReflection& reflection, bool optional = false)
{
    T resources{};
    fields(resources, [&]<class Handle>(std::string_view name, Handle&)
    { reflection.output(name, kind<Handle>(), optional); });
}

template <class T>
T read(const PassResources& ports)
{
    T result{};
    fields(result, [&]<class Handle>(std::string_view name, Handle& handle)
    {
        if constexpr (std::is_same_v<Handle, RenderGraph::ImageHandle>)
            handle = ports.image(name);
        else
            handle = ports.buffer(name);
    });
    return result;
}

template <class T>
T read(const GraphResources& resources)
{
    T result{};
    fields(result, [&]<class Handle>(std::string_view name, Handle& handle)
    {
        const auto resource = resources.find(name);
        CHECK(resource != resources.end() && std::holds_alternative<Handle>(resource->second),
              "render graph must expose '{}'", name);
        handle = std::get<Handle>(resource->second);
    });
    return result;
}

template <class T>
void write(PassResources& ports, const T& resources)
{
    fields(resources, [&](std::string_view name, auto handle) { ports.setOutput(name, handle); });
}

template <class T>
void insert(GraphResources& ports, std::string_view prefix, const T& resources)
{
    fields(resources, [&](std::string_view name, auto handle)
    { ports.emplace(std::string{prefix}.append(name), handle); });
}
}
