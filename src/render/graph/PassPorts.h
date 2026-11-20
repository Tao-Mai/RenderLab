#pragma once

#include "render/graph/RenderPass.h"

#include <meta>
#include <type_traits>

namespace pass_ports
{
template <class T, class Visit>
void fields(T& value, Visit&& visit)
{
    using Resources = std::remove_cv_t<T>;
    constexpr auto kAccess = std::meta::access_context::current();
    template for (constexpr auto kMember :
        std::define_static_array(std::meta::nonstatic_data_members_of(^^Resources, kAccess)))
    {
        visit(std::meta::identifier_of(kMember), value.[:kMember:]);
    }
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
