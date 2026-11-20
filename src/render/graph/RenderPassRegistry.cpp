#include "render/graph/RenderPassRegistry.h"

#include <vector>

namespace
{
using json = nlohmann::json;

const json& field(const json& object, std::string_view name)
{
    CHECK(object.is_object(), "expected a graph object with field '{}'", name);
    const auto found = object.find(name);
    CHECK(found != object.end(), "missing graph field '{}'", name);
    return *found;
}

const std::string& nameOf(const json& value)
{
    const auto* name = value.get_ptr<const std::string*>();
    CHECK(name && !name->empty(), "graph names must be nonempty strings");
    return *name;
}

const json& resolve(const json& values, const json& reference)
{
    CHECK(reference.is_array() && reference.size() >= 2, "invalid graph reference {}", reference.dump());
    const json* value = &values;
    for (const auto& part : reference)
        value = &field(*value, nameOf(part));
    CHECK(
        !value->is_array() || (*value)[1] != RenderGraph::kInvalidHandle,
        "graph resource {} is unavailable in this configuration",
        reference.dump()
    );
    return *value;
}
}

nlohmann::json RenderPassRegistry::build(
    RenderGraph& graph, const PassSetup& setup, const nlohmann::json& description, nlohmann::json inputs
)
{
    const auto& nodes   = field(description, "nodes");
    const auto& outputs = field(description, "outputs");
    CHECK(nodes.is_array() && outputs.is_object(), "invalid render graph description");

    active.clear();
    json values{{"inputs", std::move(inputs)}, {"passes", json::object()}};
    auto& results = values["passes"];
    for (const auto& node : nodes)
    {
        const auto& type       = nameOf(field(node, "type"));
        const auto& name       = nameOf(field(node, "name"));
        const auto& references = field(node, "inputs");
        const auto& properties = field(node, "properties");
        CHECK(references.is_array(), "inputs of pass '{}' must be an array", name);
        CHECK(properties.is_object(), "properties of pass '{}' must be an object", name);
        CHECK(!results.contains(name), "pass instance '{}' is already in the graph", name);
        const auto factory = factories.find(type);
        CHECK(factory != factories.end(), "unknown render pass type '{}'", type);

        std::vector<const json*> arguments;
        arguments.reserve(references.size());
        for (const auto& reference : references)
            arguments.push_back(&resolve(values, reference));

        auto [instance, added] = instances.try_emplace(std::pair{type, name});
        if (added)
            instance->second = factory->second.create();
        auto* pass = instance->second.get();
        auto output = factory->second.setup(pass, graph, setup, arguments, properties);
        results[name] = std::move(output);
        active.try_emplace(factory->second.type, pass);
    }

    auto result = json::object();
    for (const auto& [name, reference] : outputs.items())
        result[name] = resolve(values, reference);
    return result;
}

void RenderPassRegistry::clearInstances() noexcept
{
    active.clear();
    instances.clear();
}
