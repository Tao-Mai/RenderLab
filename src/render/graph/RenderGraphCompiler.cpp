#include "render/graph/RenderGraphCompiler.h"

#include "core/Logger.h"

#include <algorithm>
#include <queue>

namespace
{
using Json = nlohmann::json;
constexpr std::string_view kExternalNode = "$inputs";

const Json& field(const Json& object, std::string_view name)
{
    CHECK(object.is_object(), "expected graph object with field '{}'", name);
    const auto found = object.find(name);
    CHECK(found != object.end(), "missing graph field '{}'", name);
    return *found;
}

const std::string& nameOf(const Json& json)
{
    const auto* name = json.get_ptr<const std::string*>();
    CHECK(name && !name->empty(), "graph names must be nonempty strings");
    return *name;
}

struct Endpoint
{
    std::string_view node;
    std::string_view port;
};

Endpoint endpoint(const Json& value)
{
    const std::string_view name = nameOf(value);
    const auto dot = name.find('.');
    CHECK(dot != std::string_view::npos && dot > 0 && dot + 1 < name.size() &&
              name.find('.', dot + 1) == std::string_view::npos,
          "invalid render graph endpoint '{}'", name);
    return {name.substr(0, dot), name.substr(dot + 1)};
}

void mergeProperties(Json& properties, const Json& overrides)
{
    for (const auto& [name, value] : overrides.items())
    {
        auto& property = properties[name];
        if (property.is_object() && value.is_object())
            mergeProperties(property, value);
        else
            property = value;
    }
}

bool configure(const GraphPassInstance& instance, const PassOverrides& overrides)
{
    auto properties = instance.scriptedProperties;
    if (const auto type = overrides.find(instance.type); type != overrides.end())
        if (const auto found = type->second.find(instance.name); found != type->second.end())
        {
            CHECK(found->second.is_object(), "invalid overrides for pass '{}'", instance.name);
            mergeProperties(properties, found->second);
        }
    return properties != instance.pass->properties() && instance.pass->setProperties(properties);
}

PortKind resourceKind(const GraphResource& resource)
{
    return std::holds_alternative<RenderGraph::ImageHandle>(resource) ? PortKind::Image : PortKind::Buffer;
}

GraphResource emptyResource(PortKind kind)
{
    if (kind == PortKind::Image)
        return RenderGraph::ImageHandle{};
    return RenderGraph::BufferHandle{};
}

bool available(const GraphResource& resource)
{
    return std::visit([](auto handle) { return handle.id != RenderGraph::kInvalidHandle; }, resource);
}

const RenderPassPort& port(std::span<const RenderPassPort> ports, Endpoint name)
{
    const auto found = std::ranges::find(ports, name.port, &RenderPassPort::name);
    CHECK(found != ports.end(), "unknown render graph port '{}.{}'", name.node, name.port);
    return *found;
}

void validateResource(
    RenderGraph& graph, const GraphResource& resource, const RenderPassPort& declaration, std::string_view node
)
{
    CHECK(resourceKind(resource) == declaration.kind, "resource kind mismatch at '{}.{}'", node, declaration.name);
    CHECK(available(resource) || declaration.optional, "resource '{}.{}' is unavailable", node, declaration.name);
    if (available(resource) && declaration.format != vk::Format::eUndefined)
        CHECK(graph.imageInfo(std::get<RenderGraph::ImageHandle>(resource)).format == declaration.format,
              "image format mismatch at '{}.{}'", node, declaration.name);
}

struct Node
{
    GraphPassInstance* instance;
    RenderPassReflection reflection;
    std::map<std::string, Endpoint, std::less<>> inputs;
    GraphResources outputs;
    std::vector<size_t> consumers;
    std::vector<size_t> producers;
    size_t remaining = 0;
    bool live = false;
};
}

GraphResources RenderGraphCompiler::compile(
    RenderGraph& graph, const PassSetup& setup, const Json& description, const GraphResources& external,
    const PluginManager& plugins, const PassOverrides& overrides
)
{
    const auto& declarations = field(description, "nodes");
    const auto& edges = field(description, "edges");
    const auto& outputs = field(description, "outputs");
    CHECK(declarations.is_array() && edges.is_array() && outputs.is_object(), "invalid render graph description");

    active.clear();
    std::vector<Node> nodes;
    nodes.reserve(declarations.size());
    std::map<std::string, size_t, std::less<>> indices;
    for (const auto& declaration : declarations)
    {
        const auto& name = nameOf(field(declaration, "name"));
        const auto& type = nameOf(field(declaration, "type"));
        const auto& properties = field(declaration, "properties");
        const auto& ui = field(declaration, "ui");
        CHECK(name != kExternalNode && name.find('.') == std::string::npos, "invalid pass instance name '{}'", name);
        const bool inserted = indices.try_emplace(name, nodes.size()).second;
        CHECK(inserted && properties.is_object() && ui.is_boolean(), "invalid or duplicate pass instance '{}'", name);

        auto [entry, added] = instances.try_emplace(std::pair{type, name});
        auto& instance = entry->second;
        if (added)
        {
            auto pass = RenderPass::create(plugins, type);
            auto defaults = pass->properties();
            instance = {name, type, std::move(pass), std::move(defaults), {}, false};
        }
        instance.scriptedProperties = instance.defaults;
        mergeProperties(instance.scriptedProperties, properties);
        instance.exposeEditor = ui.get<bool>();
        configure(instance, overrides);
        nodes.push_back({&instance, instance.pass->reflect(setup)});
    }

    const auto indexOf = [&](std::string_view name)
    {
        const auto found = indices.find(name);
        CHECK(found != indices.end(), "unknown render pass instance '{}'", name);
        return found->second;
    };
    const auto externalResource = [&](std::string_view name) -> const GraphResource&
    {
        const auto found = external.find(name);
        CHECK(found != external.end(), "unknown render graph input '{}'", name);
        return found->second;
    };

    for (const auto& edge : edges)
    {
        CHECK(edge.is_array() && edge.size() == 2, "invalid render graph edge");
        const auto source = endpoint(edge[0]);
        const auto target = endpoint(edge[1]);
        const auto targetIndex = indexOf(target.node);
        auto& consumer = nodes[targetIndex];
        const auto& input = port(consumer.reflection.inputs(), target);
        const bool inserted = consumer.inputs.emplace(target.port, source).second;
        CHECK(inserted, "render graph input '{}.{}' is connected twice", target.node, target.port);
        if (source.node == kExternalNode)
        {
            CHECK(resourceKind(externalResource(source.port)) == input.kind,
                  "resource kind mismatch at '{}.{}'", target.node, target.port);
        }
        else
        {
            const auto sourceIndex = indexOf(source.node);
            auto& producer = nodes[sourceIndex];
            const auto& output = port(producer.reflection.outputs(), source);
            CHECK(output.kind == input.kind, "resource kind mismatch at '{}.{}'", target.node, target.port);
            producer.consumers.push_back(targetIndex);
            consumer.producers.push_back(sourceIndex);
        }
    }

    std::vector<size_t> live;
    const auto mark = [&](size_t index)
    {
        if (!nodes[index].live)
        {
            nodes[index].live = true;
            live.push_back(index);
        }
    };
    for (const auto& output : outputs)
    {
        const auto source = endpoint(output);
        if (source.node != kExternalNode)
        {
            const auto index = indexOf(source.node);
            port(nodes[index].reflection.outputs(), source);
            mark(index);
        }
    }
    for (size_t entry = 0; entry < live.size(); ++entry)
        for (size_t producer : nodes[live[entry]].producers)
            mark(producer);

    std::queue<size_t> ready;
    for (size_t index = 0; index < nodes.size(); ++index)
    {
        auto& node = nodes[index];
        node.remaining = node.producers.size();
        if (node.remaining == 0)
            ready.push(index);
    }
    const auto resolve = [&](Endpoint source) -> const GraphResource&
    {
        if (source.node == kExternalNode)
            return externalResource(source.port);
        const auto& produced = nodes[indexOf(source.node)].outputs;
        const auto resource = produced.find(source.port);
        CHECK(resource != produced.end(), "render graph output '{}.{}' is unavailable", source.node, source.port);
        return resource->second;
    };

    size_t sorted = 0;
    while (!ready.empty())
    {
        const auto index = ready.front();
        ready.pop();
        auto& node = nodes[index];
        auto& instance = *node.instance;
        if (node.live)
        {
            GraphResources inputs;
            for (const auto& input : node.reflection.inputs())
            {
                const auto source = node.inputs.find(input.name);
                auto resource = source != node.inputs.end() ? resolve(source->second) : emptyResource(input.kind);
                validateResource(graph, resource, input, instance.name);
                inputs.emplace(input.name, resource);
            }

            PassResources resources{std::move(inputs)};
            if (instance.pass->execution() == PassExecution::Composite)
                instance.pass->compile(graph, setup, resources);
            else
                graph.addStage(instance.name, *instance.pass, setup, resources);
            node.outputs = resources.outputs();
            for (const auto& output : node.reflection.outputs())
            {
                const auto resource = node.outputs.find(output.name);
                validateResource(graph, resource != node.outputs.end() ? resource->second : emptyResource(output.kind),
                                 output, instance.name);
            }
            for (const auto& [name, resource] : node.outputs)
                port(node.reflection.outputs(), {instance.name, name});
            active.push_back(&instance);
        }
        ++sorted;
        for (const auto consumer : node.consumers)
            if (--nodes[consumer].remaining == 0)
                ready.push(consumer);
    }
    CHECK(sorted == nodes.size(), "render graph contains a cycle");

    GraphResources result;
    for (const auto& [name, output] : outputs.items())
    {
        const auto& resource = resolve(endpoint(output));
        CHECK(available(resource), "marked render graph output '{}' is unavailable", name);
        std::visit([&](auto handle) { graph.markOutput(handle); }, resource);
        result.emplace(name, resource);
    }
    return result;
}

bool RenderGraphCompiler::applyOverrides(const PassOverrides& overrides)
{
    bool rebuild = false;
    for (const auto* instance : active)
        rebuild |= configure(*instance, overrides);
    return rebuild;
}

const GraphPassInstance* RenderGraphCompiler::findPass(std::string_view name) const
{
    const auto found = std::ranges::find_if(active, [&](const auto* value) { return value->name == name; });
    return found == active.end() ? nullptr : *found;
}

void RenderGraphCompiler::clear() noexcept
{
    active.clear();
    instances.clear();
}

bool graphUsesInputPrefix(const Json& description, std::string_view prefix)
{
    const auto matches = [&](const Json& value)
    {
        const auto source = endpoint(value);
        return source.node == kExternalNode && source.port.starts_with(prefix);
    };
    for (const auto& edge : field(description, "edges"))
    {
        CHECK(edge.is_array() && edge.size() == 2, "invalid render graph edge");
        if (matches(edge[0]))
            return true;
    }
    return std::ranges::any_of(field(description, "outputs"), matches);
}

nlohmann::json passSettingsOverrides(const Json& baseline, const Json& edited)
{
    auto overrides = Json::object();
    for (const auto& [name, value] : edited.items())
    {
        const auto original = baseline.find(name);
        if (original != baseline.end() && value == *original)
            continue;
        overrides[name] = original != baseline.end() && original->is_object() && value.is_object()
            ? passSettingsOverrides(*original, value) : value;
    }
    return overrides;
}
